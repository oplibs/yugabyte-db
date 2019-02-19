// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.ShellProcessHandler.ShellResponse;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.helpers.NodeDetails;
import play.Application;
import play.api.Play;
import play.libs.Json;
import org.yaml.snakeyaml.Yaml;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

public class KubernetesWaitForPod extends AbstractTaskBase {
  public enum CommandType {
    WAIT_FOR_POD;

    public String getSubTaskGroupName() {
      switch (this) {
        case WAIT_FOR_POD:
          return UserTaskDetails.SubTaskGroupType.KubernetesWaitForPod.name();
      }
      return null;
    }
  }

  @Inject
  KubernetesManager kubernetesManager;

  @Inject
  Application application;

  // Number of iterations to wait for the pod to come up.
  private static final int MAX_ITERS = 10;

  // Time to sleep on each iteration of the pod to come up.
  private static final int SLEEP_TIME = 10;

  @Override
  public void initialize(ITaskParams params) {
    this.kubernetesManager = Play.current().injector().instanceOf(KubernetesManager.class);
    this.application = Play.current().injector().instanceOf(Application.class);
    super.initialize(params);
  }

  public static class Params extends AbstractTaskParams {
    public UUID providerUUID;
    public CommandType commandType;
    public UUID universeUUID;
    // We use the nodePrefix as Helm Chart's release name,
    // so we would need that for any sort helm operations.
    public String nodePrefix;
    public String podName = null;
  }

  protected KubernetesWaitForPod.Params taskParams() {
    return (KubernetesWaitForPod.Params)taskParams;
  }

  @Override
  public void run() {
    // TODO: add checks for the shell process handler return values.
    switch (taskParams().commandType) {
      case WAIT_FOR_POD:
        int iters = 0;
        String status;
        do {
          try {
            TimeUnit.SECONDS.sleep(SLEEP_TIME);
          } catch (InterruptedException ex) {
            // Do nothing
          }
          status = waitForPod();
          iters++;
        } while (!status.equals("Running") && iters < MAX_ITERS);
        if (MAX_ITERS > 10) {
          throw new RuntimeException("Pod " + taskParams().podName + " creation taking too long.");
        }
        break;
    }
  }

  // Waits for pods as well as the containers inside the pod.
  private String waitForPod() {
    ShellResponse podResponse = kubernetesManager.getPodStatus(taskParams().providerUUID,
        taskParams().nodePrefix, taskParams().podName);
    JsonNode podInfo = parseShellResponseAsJson(podResponse);
    JsonNode statusNode = podInfo.path("status");
    String status = statusNode.get("phase").asText();
    JsonNode podConditions = statusNode.path("conditions");
    ArrayList conditions = Json.fromJson(podConditions, ArrayList.class);
    Iterator iter = conditions.iterator();
    while (iter.hasNext()) {
      JsonNode info = Json.toJson(iter.next());
      String statusContainer = info.path("status").asText();
      if (statusContainer.equals("False")) {
        status = "Not Ready";
      }
    }
    return status;
  }
}
