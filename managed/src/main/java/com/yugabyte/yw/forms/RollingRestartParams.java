package com.yugabyte.yw.forms;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import com.fasterxml.jackson.annotation.JsonIgnore;

import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import play.data.validation.Constraints;

public class RollingRestartParams extends UniverseDefinitionTaskParams {

  // The universe that we want to perform a rolling restart on.
  @Constraints.Required()
  public UUID universeUUID;

  // Rolling Restart task type
  @Constraints.Required()
  public UpgradeUniverse.UpgradeTaskType taskType;

  // The software version to install. Do not set this value if no software needs to be installed.
  public String ybSoftwareVersion = null;

  public Map<String, String> masterGFlags = new HashMap<String, String>();
  public Map<String, String> tserverGFlags = new HashMap<String, String>();

  // The nodes that we want to perform the operation and the subsequent rolling restart on.
  @Constraints.Required()
  public List<String> nodeNames;

  public Integer sleepAfterMasterRestartMillis = 180000;
  public Integer sleepAfterTServerRestartMillis = 180000;
}
