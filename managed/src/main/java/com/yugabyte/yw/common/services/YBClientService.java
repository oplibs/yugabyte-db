// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.services;

import com.yugabyte.yw.common.services.config.YbClientConfig;
import org.yb.client.YBClient;

public interface YBClientService {

  YBClient getClient(String masterHostPorts);

  YBClient getClient(String masterHostPorts, String certFile);

  YBClient getClientWithConfig(YbClientConfig config);

  void closeClient(YBClient client, String masterHostPorts);
}
