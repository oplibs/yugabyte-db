// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.Util;

public class AnsibleSetupServer extends NodeTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(AnsibleSetupServer.class);

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The VPC into which the node is to be provisioned.
    public String subnetId;
  }

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void run() {
    String ybDevopsHome = Util.getDevopsHome();
    String command = ybDevopsHome + "/ybops/ybops/scripts/yb_server_provision.py" +
                     " --cloud " + taskParams().cloud +
                     " " + taskParams().nodeName;

    // Add the appropriate VPC ID parameter if this is an AWS deployment.
    if (taskParams().cloud == CloudType.aws) {
      command += " --extra-vars {\"aws_vpc_subnet_id\":\"" + taskParams().subnetId + "\"}";
    }
    // Execute the ansible command.
    execCommand(command);
  }
}
