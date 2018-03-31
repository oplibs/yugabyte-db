//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// The PgSemState module defines the states of semantic process for expressions. Semantic states are
// different from semantic context.
// - The states consists of attributes that are used to process a tree node.
// - The context consists of attributes that are used for the entire compilation.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_PTREE_PG_SEM_STATE_H_
#define YB_YQL_PGSQL_PTREE_PG_SEM_STATE_H_

#include "yb/yql/pgsql/util/pg_env.h"
#include "yb/yql/pgsql/ptree/pg_process_context.h"
#include "yb/yql/pgsql/ptree/column_desc.h"

namespace yb {
namespace pgsql {

//--------------------------------------------------------------------------------------------------
// This class represents the state variables for the analyzing process of one tree node. This
// is just a stack varible that is constructed when a treenode is being processed and destructed
// when that process is done.
//
// Example:
// - Suppose user type the following statements
//     CREATE TABLE tab(id INT PRIMARY KEY);
//     INSERT INTO tab(id) values(expr);
// - When analyzing INSERT, we would do the following.
//   {
//     // Create a new state for compile_context.
//     PgSemState new_expr_state(compile_context, DataType::INT);
//
//     // Run express analyzer knowing that its expected type INT (=== targeted column type).
//     expr->Analyze(compile_context);
//
//     // When exiting this scope, sem_state are auto-swiched back to the previous state.
//   }
//
class PgSemState {
 public:
  // Constructor: Create a new sem_state to use and save the existing state to previous_state_.
  PgSemState(PgCompileContext *compile_context,
           const std::shared_ptr<QLType>& expected_ql_type = QLType::Create(UNKNOWN_DATA),
           InternalType expected_internal_type = InternalType::VALUE_NOT_SET,
           const ColumnDesc *lhs_col = nullptr);

  // Destructor: Reset compile_context back to previous_state_.
  virtual ~PgSemState();

  // Read previous state.
  const PgSemState *previous_state() const {
    return previous_state_;
  }

  // Reset the compile_context back to its previous state.
  void ResetContextState();

  // Update the expr states.
  void SetExprState(const std::shared_ptr<QLType>& ql_type,
                    InternalType internal_type,
                    const ColumnDesc *lhs_col = nullptr);

  // Set the current state using previous state's values.
  void CopyPreviousStates();

  // Access function for expression states.
  const std::shared_ptr<QLType>& expected_ql_type() const { return expected_ql_type_; }
  InternalType expected_internal_type() const { return expected_internal_type_; }

  // Return the hash column descriptor on LHS if available.
  const ColumnDesc *lhs_col() const { return lhs_col_; }
  const ColumnDesc *hash_col() const {
    return lhs_col_ != nullptr && lhs_col_->is_hash() ? lhs_col_ : nullptr;
  }

  bool processing_set_clause() const { return processing_set_clause_; }
  void set_processing_set_clause(bool value) { processing_set_clause_ = value; }

  bool processing_assignee() const { return processing_assignee_; }
  void set_processing_assignee(bool value) { processing_assignee_ = value; }

  void set_processing_column_definition(bool val) { processing_column_definition_ = val; }
  bool processing_column_definition() const { return processing_column_definition_; }

  bool allowing_aggregate() const {
    return allowing_aggregate_;
  }
  void set_allowing_aggregate(bool val) {
    allowing_aggregate_ = val;
  }

 private:
  // Context that owns this PgSemState.
  PgCompileContext *compile_context_;

  // Save the previous state to reset when done.
  PgSemState *previous_state_ = nullptr;
  bool was_reset = false;

  // States to process an expression node.
  std::shared_ptr<QLType> expected_ql_type_; // The expected sql type of an expression.
  InternalType expected_internal_type_;        // The expected internal type of an expression.

  // Predicate for processing a column definition in a table.
  bool processing_column_definition_ = false;

  // Descriptor for the LHS column.
  const ColumnDesc *lhs_col_ = nullptr;

  // State variable for set clause.
  bool processing_set_clause_ = false;

  // State variable for assignee.
  bool processing_assignee_ = false;

  // State variable for aggregate function.
  bool allowing_aggregate_ = false;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_PTREE_PG_SEM_STATE_H_
