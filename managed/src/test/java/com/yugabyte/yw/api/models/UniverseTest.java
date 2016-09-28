// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.api.models;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.Sets;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.UniverseDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import org.junit.Before;
import org.junit.Test;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

public class UniverseTest extends FakeDBApplication {
  private Provider defaultProvider;
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = Customer.create("Test", "test@test.com", "foo");
    defaultProvider = Provider.create("aws", "Amazon");
  }

  @Test
  public void testCreate() {
    Universe u = Universe.create("Test Universe", defaultCustomer.getCustomerId());
    assertNotNull(u);
    assertThat(u.universeUUID, is(allOf(notNullValue(), equalTo(u.universeUUID))));
    assertThat(u.version, is(allOf(notNullValue(), equalTo(1))));
    assertThat(u.name, is(allOf(notNullValue(), equalTo("Test Universe"))));
    assertThat(u.getUniverseDetails(), is(notNullValue()));
  }

  @Test
  public void testGetSingleUniverse() {
    Universe newUniverse = Universe.create("Test Universe", defaultCustomer.getCustomerId());
    assertNotNull(newUniverse);
    Universe fetchedUniverse = Universe.get(newUniverse.universeUUID);
    assertNotNull(fetchedUniverse);
    assertEquals(fetchedUniverse, newUniverse);
  }

  @Test
  public void testGetMultipleUniverse() {
    Universe u1 = Universe.create("Universe1", defaultCustomer.getCustomerId());
    Universe u2 = Universe.create("Universe2", defaultCustomer.getCustomerId());
    Universe u3 = Universe.create("Universe3", defaultCustomer.getCustomerId());
    Set<UUID> uuids = Sets.newHashSet(u1.universeUUID, u2.universeUUID, u3.universeUUID);

    Set<Universe> universes = Universe.get(uuids);
    assertNotNull(universes);
    assertEquals(universes.size(), 3);
  }

  @Test(expected = RuntimeException.class)
  public void testGetUnknownUniverse() {
    UUID unknownUUID = UUID.randomUUID();
    Universe u = Universe.get(unknownUUID);
  }

  @Test
  public void testSaveDetails() {
    Universe u = Universe.create("Test Universe", defaultCustomer.getCustomerId());

    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();

        // Create some subnets.
        List<String> subnets = new ArrayList<String>();
        subnets.add("subnet-1");
        subnets.add("subnet-2");
        subnets.add("subnet-3");

        // Add a desired number of nodes.
        universeDetails.numNodes = 5;
        for (int idx = 1; idx <= universeDetails.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.az = "az-" + idx;
          node.cloudInfo.region = "test-region";
          node.cloudInfo.subnet_id = subnets.get(idx % subnets.size());
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsMap.put(node.nodeName, node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    u = Universe.saveDetails(u.universeUUID, updater);

    int idx = 1;
    for (NodeDetails node : u.getMasters()) {
      assertTrue(node.isMaster);
      assertThat(node.nodeName, is(allOf(notNullValue(), equalTo("host-n" + idx))));
      idx++;
    }

    idx = 1;
    for (NodeDetails node : u.getTServers()) {
      assertTrue(node.isTserver);
      assertThat(node.nodeName, is(allOf(notNullValue(), equalTo("host-n" + idx))));
      idx++;
    }

    assertTrue(u.getTServers().size() > u.getMasters().size());
    assertEquals(u.getMasters().size(), 3);
    assertEquals(u.getTServers().size(), 5);
  }

  @Test
  public void testGetMasterAddresses() {
    Universe u = Universe.create("Test Universe", defaultCustomer.getCustomerId());

    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();

        // Add a desired number of nodes.
        universeDetails.numNodes = 3;
        for (int idx = 1; idx <= universeDetails.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.az = "az-" + idx;
          node.cloudInfo.region = "test-region";
          node.cloudInfo.subnet_id = "subnet-" + idx;
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsMap.put(node.nodeName, node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    u = Universe.saveDetails(u.universeUUID, updater);
    assertThat(u.getMasterAddresses(), is(allOf(notNullValue(), equalTo("host-n1:7100,host-n2:7100,host-n3:7100"))));
  }

  @Test
  public void testToJSON() {
    Universe u = Universe.create("Test Universe", defaultCustomer.getCustomerId());

    Region r1 = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    Region r2 = Region.create(defaultProvider, "region-2", "Region 2", "yb-image-1");
    Region r3 = Region.create(defaultProvider, "region-3", "Region 3", "yb-image-1");
    List<UUID> regionList = new ArrayList<UUID>();
    regionList.add(r1.uuid);
    regionList.add(r2.uuid);
    regionList.add(r3.uuid);
    Universe.UniverseUpdater updater = new Universe.UniverseUpdater() {
      @Override
      public void run(Universe universe) {
        UniverseDetails universeDetails = universe.getUniverseDetails();
        universeDetails.userIntent = new UserIntent();
        universeDetails.userIntent.isMultiAZ = true;
        universeDetails.userIntent.replicationFactor = 3;
        universeDetails.userIntent.regionList = regionList;

        // Add a desired number of nodes.
        universeDetails.numNodes = 3;
        for (int idx = 1; idx <= universeDetails.numNodes; idx++) {
          NodeDetails node = new NodeDetails();
          node.nodeName = "host-n" + idx;
          node.cloudInfo = new CloudSpecificInfo();
          node.cloudInfo.cloud = "aws";
          node.cloudInfo.az = "az-" + idx;
          node.cloudInfo.region = "test-region";
          node.cloudInfo.subnet_id = "subnet-" + idx;
          node.cloudInfo.private_ip = "host-n" + idx;
          node.isTserver = true;
          if (idx <= 3) {
            node.isMaster = true;
          }
          node.nodeIdx = idx;
          universeDetails.nodeDetailsMap.put(node.nodeName, node);
        }
        universe.setUniverseDetails(universeDetails);
      }
    };
    u = Universe.saveDetails(u.universeUUID, updater);

    JsonNode universeJson = u.toJson();
    assertThat(universeJson.get("universeUUID").asText(), allOf(notNullValue(), equalTo(u.universeUUID.toString())));
    JsonNode userIntent = universeJson.get("universeDetails").get("userIntent");
    assertTrue(userIntent.get("regionList").isArray());
    assertEquals(3, userIntent.get("regionList").size());

    JsonNode regionsNode = universeJson.get("regions");
    assertThat(regionsNode, is(notNullValue()));
    assertTrue(regionsNode.isArray());
    assertEquals(3, regionsNode.size());

    JsonNode providerNode = universeJson.get("provider");
    assertThat(providerNode, is(notNullValue()));
    assertThat(providerNode.get("uuid").asText(), allOf(notNullValue(), equalTo(defaultProvider.uuid.toString())));
  }
}
