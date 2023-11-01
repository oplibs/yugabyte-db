// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <string>
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_entity_base.h"
#include "yb/master/sys_catalog.h"

#define DECLARE_LOADER_CLASS(name, key_type, entry_pb_name) \
  template <typename CatalogEntityWrapper> \
  class BOOST_PP_CAT(name, Loader) \
      : public Visitor<BOOST_PP_CAT(BOOST_PP_CAT(Persistent, name), Info)> { \
   public: \
    explicit BOOST_PP_CAT(name, Loader)(CatalogEntityWrapper& catalog_entity_wrapper) \
        : catalog_entity_wrapper_(catalog_entity_wrapper) {} \
   private: \
    Status Visit(const key_type& key, const entry_pb_name& metadata) override { \
      catalog_entity_wrapper_.Load(metadata); \
      return Status::OK(); \
    } \
    CatalogEntityWrapper& catalog_entity_wrapper_; \
    DISALLOW_COPY_AND_ASSIGN(BOOST_PP_CAT(name, Loader)); \
  };

namespace yb::master {

struct PersistentXClusterConfigInfo
    : public Persistent<SysXClusterConfigEntryPB, SysRowEntryType::XCLUSTER_CONFIG> {};

class XClusterConfigInfo : public SingletonMetadataCowWrapper<PersistentXClusterConfigInfo> {};

DECLARE_LOADER_CLASS(XClusterConfig, std::string, SysXClusterConfigEntryPB);

struct PersistentXClusterSafeTimeInfo
    : public Persistent<XClusterSafeTimePB, SysRowEntryType::XCLUSTER_SAFE_TIME> {};

class XClusterSafeTimeInfo : public SingletonMetadataCowWrapper<PersistentXClusterSafeTimeInfo> {
 public:
  void Load(const XClusterSafeTimePB& metadata) override;
};

DECLARE_LOADER_CLASS(XClusterSafeTime, std::string, XClusterSafeTimePB);

}  // namespace yb::master
