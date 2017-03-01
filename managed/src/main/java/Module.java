// Copyright (c) YugaByte, Inc.

import com.google.inject.AbstractModule;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.SwamperHelper;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.common.services.LocalYBClientService;
import com.yugabyte.yw.common.services.LocalYBMiniClusterService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.YBMiniClusterService;

/**
 * This class is a Guice module that tells Guice to bind different types
 *
 * Play will automatically use any class called 'Module' in the root package
 */
public class Module extends AbstractModule {
  @Override
  public void configure() {
    // Bind Application Initializer
    bind(AppInit.class).asEagerSingleton();
    bind(SwamperHelper.class).asEagerSingleton();
    // Set LocalClientService as the implementation for YBClientService
    bind(YBClientService.class).to(LocalYBClientService.class);
    // Set LocalMiniClusterService as the implementation for YBClientService
    bind(YBMiniClusterService.class).to(LocalYBMiniClusterService.class);

    bind(NodeManager.class).asEagerSingleton();
    bind(MetricQueryHelper.class).asEagerSingleton();
    bind(ShellProcessHandler.class).asEagerSingleton();
  }
}
