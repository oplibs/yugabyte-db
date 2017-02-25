// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.params;

import java.util.HashMap;
import java.util.Map;
import java.util.UUID;

import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;

public class NodeTaskParams extends UniverseTaskParams {
  // The AZ in which the node should be. This can be used to find the region.
  public UUID azUuid;

  // The node about which we need to fetch details.
  public String nodeName;

  // The UUID of the node that we have selected.
  // TODO: currently only used for onprem cloud.
  public UUID nodeUuid;

  // The type of instance for this node
  // TODO: currently only used for onprem cloud.
  public String instanceType;

  public AvailabilityZone getAZ() {
    return AvailabilityZone.find.byId(azUuid);
  }

  public Region getRegion() {
    return getAZ().region;
  }

  public Provider getProvider() { return getAZ().getProvider(); }

  // Less prominent params can be added to properties variable
  private Map<String, String> properties = new HashMap<>();
  public Map<String, String> getProperties() { return properties; }
  public void setProperty(String key, String value) { properties.put(key, value); }
  public String getProperty(String key) { return properties.getOrDefault(key, null); }
}
