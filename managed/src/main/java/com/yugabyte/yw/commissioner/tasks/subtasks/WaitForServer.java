// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;

import org.yb.client.shaded.com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import play.api.Play;

public class WaitForServer extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(WaitForServer.class);

  // The YB client.
  public YBClientService ybService = null;

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 60000;

  public static class Params extends UniverseTaskParams {
    // The name of the node which contains the server process.
    public String nodeName;

    // Server type running on the above node for which we will wait. 
    public ServerType serverType;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = (Params)params;
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().universeUUID + ", " + taskParams().nodeName +
           ", type=" + taskParams().serverType + ")";
  }

  @Override
  public void run() {
    boolean ret = false;
    if (taskParams().serverType != ServerType.MASTER && taskParams().serverType != ServerType.TSERVER) {
      throw new IllegalArgumentException("Unexpected server type " + taskParams().serverType);
    }
    String hostPorts = Universe.get(taskParams().universeUUID).getMasterAddresses();
    try {
      LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
      YBClient client = ybService.getClient(hostPorts);
      NodeDetails node = Universe.get(taskParams().universeUUID).getNode(taskParams().nodeName);

      if (taskParams().serverType == ServerType.MASTER && !node.isMaster) {
        throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is not for a " +
                                           "node running master : " + node.toString());
      }

      if (taskParams().serverType == ServerType.TSERVER && !node.isTserver) {
        throw new IllegalArgumentException("Task server type " + taskParams().serverType + " is not for a " +
                                           "node running tserver : " + node.toString());
      }

      HostAndPort hp = HostAndPort.fromParts(
          node.cloudInfo.private_ip,
          taskParams().serverType == ServerType.MASTER ? node.masterRpcPort : node.tserverRpcPort);
      ret = client.waitForServer(hp, TIMEOUT_SERVER_WAIT_MS);
    } catch (Exception e) {
      LOG.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
    if (!ret) {
      throw new RuntimeException("Server did not respond to pings in the set time.");
    }
  }
}
