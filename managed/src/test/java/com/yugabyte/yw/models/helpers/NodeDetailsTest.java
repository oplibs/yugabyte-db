// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models.helpers;

import com.yugabyte.yw.common.ApiUtils;
import org.junit.Before;
import org.junit.Test;

import java.util.HashSet;
import java.util.Set;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertTrue;

public class NodeDetailsTest {
  private NodeDetails nd;

  @Before
  public void setUp() {
    nd = ApiUtils.getDummyNodeDetails(1, NodeDetails.NodeState.Running);
  }

  @Test
  public void testToString() {
    assertThat(nd.toString(), allOf(notNullValue(),
                                    equalTo("name: host-n1, cloudInfo: az-1.test-region.aws, type: c3-large, " +
                                            "ip: host-n1, isMaster: false, isTserver: true, state: Running, " +
                                            "azUuid: null")));
  }

  @Test
  public void testIsActive() {
    Set<NodeDetails.NodeState> activeStates = new HashSet<>();
    activeStates.add(NodeDetails.NodeState.ToBeAdded);
    activeStates.add(NodeDetails.NodeState.Provisioned);
    activeStates.add(NodeDetails.NodeState.SoftwareInstalled);
    activeStates.add(NodeDetails.NodeState.UpgradeSoftware);
    activeStates.add(NodeDetails.NodeState.UpdateGFlags);
    activeStates.add(NodeDetails.NodeState.Running);
    activeStates.add(NodeDetails.NodeState.Stopping);
    for (NodeDetails.NodeState state : NodeDetails.NodeState.values()) {
      nd.state = state;
      if (activeStates.contains(state)) {
        assertTrue(nd.isActive());
      } else {
        assertFalse(nd.isActive());
      }
    }
  }

  @Test
  public void testIsQueryable() {
    Set<NodeDetails.NodeState> queryableStates = new HashSet<>();
    queryableStates.add(NodeDetails.NodeState.UpgradeSoftware);
    queryableStates.add(NodeDetails.NodeState.UpdateGFlags);
    queryableStates.add(NodeDetails.NodeState.Running);
    queryableStates.add(NodeDetails.NodeState.ToBeDecommissioned);
    queryableStates.add(NodeDetails.NodeState.BeingDecommissioned);
    queryableStates.add(NodeDetails.NodeState.Stopping);
    for (NodeDetails.NodeState state : NodeDetails.NodeState.values()) {
      nd.state = state;
      if (queryableStates.contains(state)) {
        assertTrue(nd.isQueryable());
      } else {
        assertFalse(nd.isQueryable());
      }
    }
  }
}
