/*
* Copyright 2019 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.Random;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.ybc.PingRequest;
import org.yb.ybc.PingResponse;

@Slf4j
public class WaitForYbcServer extends UniverseTaskBase {

  @Inject YbcClientService ybcService;

  private final int MAX_NUM_RETRIES = 10;
  private final Duration PING_RETRY_WAIT_TIME = Duration.ofSeconds(30);

  @Inject
  protected WaitForYbcServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    // The universe UUID must be stored in universeUUID field.
    // The xCluster info object to persist.
    public Set<String> nodeNameList = null;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    boolean isYbcConfigured = true;
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String certFile = universe.getCertificateNodetoNode();
    YbcClient client = null;
    Random rand = new Random();
    int ybcPort = universe.getUniverseDetails().communicationPorts.ybControllerrRpcPort;
    Set<NodeDetails> nodeDetailsSet =
        taskParams().nodeDetailsSet == null
            ? universe.getUniverseDetails().nodeDetailsSet
            : taskParams()
                .nodeNameList
                .stream()
                .map(nodeName -> universe.getNode(nodeName))
                .collect(Collectors.toSet());
    String errMsg = "";

    log.info("Universe uuid: {}, ybcPort: {} to be used", universe.universeUUID, ybcPort);
    for (NodeDetails node : nodeDetailsSet) {
      String nodeIp = node.cloudInfo.private_ip;
      log.info("Node IP: {} to connect to YBC", nodeIp);

      try {
        client = ybcService.getNewClient(nodeIp, ybcPort, certFile);
        if (client == null) {
          throw new Exception("Could not create Ybc client.");
        }
        log.info("Node IP: {} Client created", nodeIp);
        long seqNum = rand.nextInt();
        PingRequest pingReq = PingRequest.newBuilder().setSequence(seqNum).build();
        int numTries = 0;
        do {
          log.info("Node IP: {} Making a ping request", nodeIp);
          PingResponse pingResp = client.ping(pingReq);
          if (pingResp != null && pingResp.getSequence() == seqNum) {
            log.info("Node IP: {} Ping successful", nodeIp);
            break;
          } else if (pingResp == null) {
            numTries++;
            log.info(
                "Node IP: {} Ping not complete. Sleeping for {} seconds",
                nodeIp,
                PING_RETRY_WAIT_TIME.getSeconds());
            waitFor(PING_RETRY_WAIT_TIME);
            if (numTries <= MAX_NUM_RETRIES) {
              log.info("Node IP: {} Ping not complete. Continuing", nodeIp);
              continue;
            }
          }
          if (numTries > MAX_NUM_RETRIES) {
            log.info("Node IP: {} Ping failed. Exceeded max retries", nodeIp);
            errMsg = String.format("Exceeded max retries: %s", MAX_NUM_RETRIES);
            isYbcConfigured = false;
            break;
          } else if (pingResp.getSequence() != seqNum) {
            errMsg =
                String.format(
                    "Returned incorrect seqNum. Expected: %s, Actual: %s",
                    seqNum, pingResp.getSequence());
            isYbcConfigured = false;
            break;
          }
        } while (true);

      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
        throw new RuntimeException(e);
      } finally {
        ybcService.closeClient(client);
        if (!isYbcConfigured) {
          throw new RuntimeException(
              String.format("Exception in pinging yb-controller server at %s: %s", nodeIp, errMsg));
        }
      }
    }
  }
}
