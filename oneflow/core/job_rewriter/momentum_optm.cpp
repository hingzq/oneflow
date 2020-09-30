/*
Copyright 2020 The OneFlow Authors. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/
#include "oneflow/core/common/str_util.h"
#include "oneflow/core/job_rewriter/optimizer.h"
#include "oneflow/core/framework/framework.h"

namespace oneflow {

namespace {

void GenerateOptimizerOpConf(const VariableOp& op, const ParallelConf& parallel_conf,
                             JobBuilder* job_builder, const LogicalBlobId& diff_lbi_of_var_out) {
  const auto& train_conf = job_builder->job().job_conf().train_conf();
  const NormalModelUpdateOpUserConf& model_update_conf = train_conf.model_update_conf();

  const std::string op_name = op.op_name() + "-momentum";
  OperatorConf momentum_var(op.op_conf());
  const bool has_snapshot_path =
      job_builder->job().job_conf().has_default_initialize_with_snapshot_path();
  std::string file_path;
  if (has_snapshot_path) {
    file_path = JoinPath(job_builder->job().job_conf().default_initialize_with_snapshot_path(),
                         op_name, "out");
  }
  if (has_snapshot_path && SnapshotFS()->FileExists(file_path)) {
    LOG(INFO) << "file_path: " << file_path;
    momentum_var.mutable_variable_conf()->mutable_initialize_with_snapshot()->set_path(
        JoinPath(job_builder->job().job_conf().default_initialize_with_snapshot_path(), op_name));
    momentum_var.mutable_variable_conf()->mutable_initialize_with_snapshot()->set_key("out");
  } else {
    if (has_snapshot_path) { LOG(INFO) << file_path << " not found, will be initialized"; }
    InitializerConf constant_initializer;
    constant_initializer.mutable_constant_conf()->set_value(0.f);
    *(momentum_var.mutable_variable_conf()->mutable_initializer()) = constant_initializer;
  }
  momentum_var.set_name(op_name);
  momentum_var.mutable_variable_conf()->set_out("out");
  momentum_var.set_scope_symbol_id(op.op_conf().scope_symbol_id());
  job_builder->AddOps(parallel_conf, {momentum_var});

  user_op::UserOpConfWrapperBuilder momentum_update_op_builder(op.op_name() + "_optimizer");
  momentum_update_op_builder.OpTypeName("momentum_update")
      .Input("model", GenLogicalBlobName(op.BnInOp2Lbi("out")))
      .Input("model_diff", GenLogicalBlobName(diff_lbi_of_var_out))
      .Input("learning_rate", train_conf.primary_lr_lbn())
      .Input("momentum", GenLogicalBlobName(op_name, momentum_var.variable_conf().out()))
      .Attr<float>("beta", model_update_conf.momentum_conf().beta())
      .Attr<float>("weight_decay", GetOptimizerWeightDecayRate(model_update_conf, op))
      .ScopeSymbolId(op.op_conf().scope_symbol_id());
  user_op::UserOpConfWrapper momentum_update_op = momentum_update_op_builder.Build();
  job_builder->AddOps(parallel_conf, {momentum_update_op.op_conf()});
}

}  // namespace

REGISTER_OPTIMIZER(NormalModelUpdateOpUserConf::kMomentumConf, &GenerateOptimizerOpConf);

}  // namespace oneflow
