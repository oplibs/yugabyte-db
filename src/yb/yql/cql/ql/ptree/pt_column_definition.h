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
// Column Definition Tree node definition.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_CQL_QL_PTREE_PT_COLUMN_DEFINITION_H
#define YB_YQL_CQL_QL_PTREE_PT_COLUMN_DEFINITION_H

#include "yb/common/schema.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"
#include "yb/yql/cql/ql/ptree/pt_type.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"

namespace yb {
namespace ql {

//--------------------------------------------------------------------------------------------------
// Table column.
// Usage:
//   CREATE TABLE (column_definitions)
class PTColumnDefinition : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTColumnDefinition> SharedPtr;
  typedef MCSharedPtr<const PTColumnDefinition> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  PTColumnDefinition(MemoryContext *memctx,
                     YBLocation::SharedPtr loc,
                     const MCSharedPtr<MCString>& name,
                     const PTBaseType::SharedPtr& datatype,
                     const PTListNode::SharedPtr& qualifiers);

  virtual ~PTColumnDefinition();

  template<typename... TypeArgs>
  inline static PTColumnDefinition::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTColumnDefinition>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kPTColumnDefinition;
  }

  // Access function for is_primary_key_.
  bool is_primary_key() const {
    return is_primary_key_;
  }
  void set_is_primary_key() {
    is_primary_key_ = true;
  }

  // Access function for is_hash_key_.
  bool is_hash_key() const {
    return is_hash_key_;
  }
  void set_is_hash_key() {
    is_primary_key_ = true;
    is_hash_key_ = true;
  }

  // Access function for is_static_.
  bool is_static() const {
    return is_static_;
  }
  void set_is_static() {
    is_static_ = true;
  }

  // Access function for order_.
  int32_t order() const {
    return order_;
  }
  void set_order(int32 order) {
    order_ = order;
  }

  ColumnSchema::SortingType sorting_type() {
    return sorting_type_;
  }

  void set_sorting_type(ColumnSchema::SortingType sorting_type) {
    sorting_type_ = sorting_type;
  }

  const MCSharedPtr<MCString>& name() {
    return name_;
  }

  const char *yb_name() const {
    return name_->c_str();
  }

  const PTBaseType::SharedPtr& datatype() const {
    return datatype_;
  }

  virtual std::shared_ptr<QLType> ql_type() const {
    return datatype_->ql_type();
  }

  virtual bool is_counter() const {
    return datatype_->is_counter();
  }

  virtual const PTExpr::SharedPtr colexpr() const {
    return nullptr;
  }

  virtual int32_t indexed_ref() const {
    return indexed_ref_;
  }

  virtual void set_indexed_ref(int32_t col_id) {
    indexed_ref_ = col_id;
  }

  void AddIndexedRef(int32_t id);

 protected:
  MCSharedPtr<MCString> name_;
  PTBaseType::SharedPtr datatype_;
  PTListNode::SharedPtr qualifiers_;
  bool is_primary_key_;
  bool is_hash_key_;
  bool is_static_;
  int32_t order_;
  // Sorting order. Only relevant when this key is a primary key.
  ColumnSchema::SortingType sorting_type_;

  // Ref column_id.
  // - In a TABLE, indexed_ref_ of a column is "-1" as it doesn't reference any column.
  // - In an INDEX, this column is referencing to a column in data-table. Our current index design
  //   only allow referencing ONE column.
  //
  // Example for scalar index
  //   TABLE (a, b, c)
  //   INDEX (c) -> INDEX is a table whose column 'c' is referencing TABLE(c)
  // Example for JSON index
  //   TABLE (a, b, j)
  //   INDEX (j->>'b') -> INDEX is a table whose column 'j->>b' is referencing to TABLE(j)
  int32 indexed_ref_ = -1;
};

// IndexColumn - Name of an expression that is used for indexing.
// Usage:
//   PRIMARY KEY (index_columns)
//   INDEX ON tab (index_columns) include (index_columns)
class PTIndexColumn : public PTColumnDefinition {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTIndexColumn> SharedPtr;
  typedef MCSharedPtr<const PTIndexColumn> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  explicit PTIndexColumn(MemoryContext *memctx,
                         YBLocation::SharedPtr loc,
                         const MCSharedPtr<MCString>& name,
                         const PTExpr::SharedPtr& expr);
  virtual ~PTIndexColumn();

  template<typename... TypeArgs>
  inline static PTIndexColumn::SharedPtr MakeShared(MemoryContext *memctx, TypeArgs&&... args) {
    return MCMakeShared<PTIndexColumn>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual CHECKED_STATUS Analyze(SemContext *sem_context) override;

  CHECKED_STATUS SetupPrimaryKey(SemContext *sem_context);
  CHECKED_STATUS SetupHashKey(SemContext *sem_context);
  CHECKED_STATUS SetupCoveringIndexColumn(SemContext *sem_context);

  virtual std::shared_ptr<QLType> ql_type() const override {
    return colexpr_->ql_type();
  }

  virtual bool is_counter() const override {
    return false;
  }

  virtual const PTExpr::SharedPtr colexpr() const override {
    return colexpr_;
  }

 private:
  // Indexing column is computed based on the value of an expression.
  // Example:
  //   - TABLE tab(a, j)
  //   - INDEX (j->>'x') ==> colexpr_ = j->'x'
  //   - When user insert to TABLE tab, YugaByte inserts to INDEX (j->'x', ybctid)
  //   - DEFAULT: colexpr == EXPR_NOT_YET
  PTExpr::SharedPtr colexpr_;

  // "coldef_" is pointer to the definition of an index column.
  // - If the column of the same name is previously-defined, we use that definition.
  // - Otherwise, we use this node as the column definition.
  PTColumnDefinition *coldef_ = nullptr;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_YQL_CQL_QL_PTREE_PT_COLUMN_DEFINITION_H
