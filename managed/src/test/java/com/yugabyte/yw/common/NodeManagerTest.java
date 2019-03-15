// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.commissioner.tasks.subtasks.InstanceActions;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;

import play.libs.Json;

import java.io.File;
import java.util.ArrayList;
import java.util.Calendar;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Predicate;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Download;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Install;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.Everything;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.GFlags;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.Software;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.fail;
import org.mockito.ArgumentCaptor;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.mockito.Matchers.any;


@RunWith(MockitoJUnitRunner.class)
public class NodeManagerTest extends FakeDBApplication {

  @Mock
  play.Configuration mockAppConfig;

  @Mock
  ShellProcessHandler shellProcessHandler;

  @Mock
  ReleaseManager releaseManager;

  @InjectMocks
  NodeManager nodeManager;
  
  private final String DOCKER_NETWORK = "yugaware_bridge";
  private final String MASTER_ADDRESSES = "host-n1:7100,host-n2:7100,host-n3:7100";
  private final String fakeMountPath1 = "/fake/path/d0";
  private final String fakeMountPath2 = "/fake/path/d1";
  private final String fakeMountPaths = fakeMountPath1 + "," + fakeMountPath2;
  private final String instanceTypeCode = "fake_instance_type";

  private class TestData {
    public Common.CloudType cloudType;
    public PublicCloudConstants.StorageType storageType;
    public Provider provider;
    public Region region;
    public AvailabilityZone zone;
    public NodeInstance node;
    public List<String> baseCommand = new ArrayList<>();

    public TestData(Provider p, Common.CloudType cloud, PublicCloudConstants.StorageType storageType, int idx) {
      cloudType = cloud;
      this.storageType = storageType;
      provider = p;
      region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");

      NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
      nodeData.ip = "fake_ip";
      nodeData.region = region.code;
      nodeData.zone = zone.code;
      nodeData.instanceType = instanceTypeCode;
      node = NodeInstance.create(zone.uuid, nodeData);
      // Update name.
      node.setNodeName("host-n" + idx);
      node.save();

      baseCommand.add("bin/ybcloud.sh");
      baseCommand.add(provider.code);
      baseCommand.add("--region");
      baseCommand.add(region.code);
      baseCommand.add("--zone");
      baseCommand.add(zone.code);

      if (cloudType == Common.CloudType.docker) {
        baseCommand.add("--network");
        baseCommand.add(DOCKER_NETWORK);
      }

      if (cloudType == Common.CloudType.onprem) {
        baseCommand.add("--node_metadata");
        baseCommand.add(node.getDetailsJson());
      }
    }
  }

  private List<TestData> testData;

  public List<TestData> getTestData(Customer customer, Common.CloudType cloud) {
    List<TestData> testDataList = new ArrayList<>();
    Provider provider = ModelFactory.newProvider(customer, cloud);
    if (cloud.equals(Common.CloudType.aws)) {
      testDataList.add(new TestData(provider, cloud, PublicCloudConstants.StorageType.GP2, 1));
    }
    else if (cloud.equals(Common.CloudType.gcp)) {
      testDataList.add(new TestData(provider, cloud, PublicCloudConstants.StorageType.IO1, 2));
    } else {
      testDataList.add(new TestData(provider, cloud, null, 3));
    }
    return testDataList;
  }

  private Universe createUniverse() {
    UUID universeUUID = UUID.randomUUID();
    return ModelFactory.createUniverse("Test Universe - " + universeUUID, universeUUID);
  }

  private void buildValidParams(TestData testData, NodeTaskParams params, Universe universe) {
    params.azUuid = testData.zone.uuid;
    params.instanceType = testData.node.instanceTypeCode;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = universe.universeUUID;
    params.placementUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
  }

  private void addValidDeviceInfo(TestData testData, NodeTaskParams params) {
    params.deviceInfo = new DeviceInfo();
    params.deviceInfo.mountPoints = fakeMountPaths;
    params.deviceInfo.volumeSize = 200;
    params.deviceInfo.numVolumes = 2;
    if (testData.cloudType.equals(Common.CloudType.aws)) {
      params.deviceInfo.storageType = testData.storageType;
      if (testData.storageType != null && testData.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
        params.deviceInfo.diskIops = 240;
      }
    }
  }

  private NodeTaskParams createInvalidParams(TestData testData) {
    Universe u = createUniverse();
    NodeTaskParams params = new NodeTaskParams();
    params.azUuid = testData.zone.uuid;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = u.universeUUID;
    return params;
  }

  private AccessKey getOrCreate(UUID providerUUID, String keyCode, AccessKey.KeyInfo keyInfo) {
    AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
    if (accessKey == null) {
      accessKey = AccessKey.create(providerUUID, keyCode, keyInfo);
    }
    return accessKey;
  }

  private UUID createUniverseWithCert(TestData t, AnsibleConfigureServers.Params params){
    Calendar cal = Calendar.getInstance();
    Date today = cal.getTime();
    cal.add(Calendar.YEAR, 1); // to get previous year add -1
    Date nextYear = cal.getTime();
    UUID rootCAuuid = UUID.randomUUID();
    CertificateInfo cert = CertificateInfo.create(rootCAuuid,
      t.provider.customerUUID,
      params.nodePrefix,
      today,
      nextYear,
      "/path/to/private.key",
      "/path/to/cert.crt");
    Universe u = createUniverse();
    u.getUniverseDetails().rootCA = cert.uuid;
    buildValidParams(t, params, Universe.saveDetails(u.universeUUID,
      ApiUtils.mockUniverseUpdater(t.cloudType)));
    return cert.uuid;
  }

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    testData = new ArrayList<TestData>();
    testData.addAll(getTestData(customer, Common.CloudType.aws));
    testData.addAll(getTestData(customer, Common.CloudType.gcp));
    testData.addAll(getTestData(customer, Common.CloudType.onprem));
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
    ReleaseManager.ReleaseMetadata releaseMetadata = new ReleaseManager.ReleaseMetadata();
    releaseMetadata.filePath = "/yb/release.tar.gz";
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn(releaseMetadata);
  }

  private List<String> nodeCommand(
      NodeManager.NodeCommandType type, NodeTaskParams params, TestData testData) {
    Common.CloudType cloud = testData.cloudType;
    List<String> expectedCommand = new ArrayList<>();

    expectedCommand.add("instance");
    expectedCommand.add(type.toString().toLowerCase());
    switch (type) {
      case List:
        expectedCommand.add("--as_json");
        break;
      case Control:
        AnsibleClusterServerCtl.Params ctlParams = (AnsibleClusterServerCtl.Params) params;
        expectedCommand.add(ctlParams.process);
        expectedCommand.add(ctlParams.command);
        break;
      case Provision:
        AnsibleSetupServer.Params setupParams = (AnsibleSetupServer.Params) params;
        if (!cloud.equals(Common.CloudType.onprem)) {
          expectedCommand.add("--instance_type");
          expectedCommand.add(setupParams.instanceType);
          expectedCommand.add("--cloud_subnet");
          expectedCommand.add(setupParams.subnetId);
          String ybImage = testData.region.ybImage;
          if (ybImage != null && !ybImage.isEmpty()) {
            expectedCommand.add("--machine_image");
            expectedCommand.add(ybImage);
          }
          if (setupParams.assignPublicIP) {
            expectedCommand.add("--assign_public_ip");
          }
        }
        if (cloud.equals(Common.CloudType.aws)) {
          if (setupParams.useTimeSync) {
            expectedCommand.add("--use_chrony");
	      }
	      if (!setupParams.clusters.isEmpty() && setupParams.clusters.get(0) != null &&
              !setupParams.clusters.get(0).userIntent.instanceTags.isEmpty()) {
            expectedCommand.add("--instance_tags");
            expectedCommand.add(Json.stringify(
                Json.toJson(setupParams.clusters.get(0).userIntent.instanceTags)));
          }
        }
        break;
      case Configure:
        AnsibleConfigureServers.Params configureParams = (AnsibleConfigureServers.Params) params;

        expectedCommand.add("--master_addresses_for_tserver");
        expectedCommand.add(MASTER_ADDRESSES);
        if (!configureParams.isMasterInShellMode) {
          expectedCommand.add("--master_addresses_for_master");
          expectedCommand.add(MASTER_ADDRESSES);
        }
        if (configureParams.ybSoftwareVersion != null) {
          expectedCommand.add("--package");
          expectedCommand.add("/yb/release.tar.gz");
        }

        if (configureParams.getProperty("taskSubType") != null) {
          UpgradeUniverse.UpgradeTaskSubType taskSubType =
              UpgradeUniverse.UpgradeTaskSubType.valueOf(configureParams.getProperty("taskSubType"));
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());
          switch(taskSubType) {
            case Download:
              expectedCommand.add("--tags");
              expectedCommand.add("download-software");
              break;
            case Install:
              expectedCommand.add("--tags");
              expectedCommand.add("install-software");
              break;
            case None:
              break;
          }
        }

        Map<String, String> gflags = new HashMap<>(configureParams.gflags);
        if (!configureParams.isMaster) {
          gflags.put("placement_uuid", String.valueOf(params.placementUuid));
        }
        gflags.put("metric_node_name", params.nodeName);
        if (configureParams.type == Everything) {
          if (configureParams.enableYSQL) {
            gflags.put("start_pgsql_proxy", "true");
            gflags.put("pgsql_proxy_bind_address", String.format("%s:%s", configureParams.nodeName,
              Universe.get(configureParams.universeUUID)
                .getNode(configureParams.nodeName).ysqlServerRpcPort));
          }
          if (configureParams.callhomeLevel != null) {
            gflags.put("callhome_collection_level", configureParams.callhomeLevel.toString().toLowerCase());
            if (configureParams.callhomeLevel.toString() == "NONE") {
              gflags.put("callhome_enabled", "false");
            }
          }
          if (configureParams.enableNodeToNodeEncrypt || configureParams.enableClientToNodeEncrypt) {
            CertificateInfo cert = CertificateInfo.get(configureParams.rootCA);
            if (cert == null) {
              throw new RuntimeException("No valid rootCA found for " + configureParams.universeUUID);
            }
            if (configureParams.enableNodeToNodeEncrypt) gflags.put("use_node_to_node_encryption", "true");
            if (configureParams.enableClientToNodeEncrypt) gflags.put("use_client_to_server_encryption", "true");
            gflags.put("allow_insecure_connections", "true");
            gflags.put("certs_dir", "/home/yugabyte/yugabyte-tls-config");
            expectedCommand.add("--rootCA_cert");
            expectedCommand.add(cert.certificate);
            expectedCommand.add("--rootCA_key");
            expectedCommand.add(cert.privateKey);
          }
          expectedCommand.add("--extra_gflags");
          expectedCommand.add(Json.stringify(Json.toJson(gflags)));
        } else if (configureParams.type == GFlags) {
          if (!configureParams.gflags.isEmpty()) {
            String processType = configureParams.getProperty("processType");
            expectedCommand.add("--yb_process_type");
            expectedCommand.add(processType.toLowerCase());
            String gflagsJson =  Json.stringify(Json.toJson(gflags));
            expectedCommand.add("--replace_gflags");
            expectedCommand.add("--gflags");
            expectedCommand.add(gflagsJson);
          }
        } else {
          expectedCommand.add("--extra_gflags");
          Map<String, String> gflags1 = new HashMap<>(configureParams.gflags);
          gflags1.put("placement_uuid", String.valueOf(params.placementUuid));
          expectedCommand.add(Json.stringify(Json.toJson(gflags1)));
        }
        break;
      case Destroy:
        expectedCommand.add("--instance_type");
        expectedCommand.add(instanceTypeCode);
        break;
      case Tags:
        InstanceActions.Params tagsParams = (InstanceActions.Params)params;
        if (cloud.equals(Common.CloudType.aws)) {
          expectedCommand.add("--instance_tags");
          // The quotes in format is needed here, so cannot use instanceTags.toString().
          expectedCommand.add("{\"Cust\":\"Test\"}");
          if (!tagsParams.deleteTags.isEmpty()) {
            expectedCommand.add("--remove_tags");
            expectedCommand.add(tagsParams.deleteTags);            
          }
        }
        break;
      case InitYSQL:
        InstanceActions.Params initYSQLParams = (InstanceActions.Params)params;
        expectedCommand.add("--master_addresses");
        expectedCommand.add(Universe.get(initYSQLParams.universeUUID).getMasterAddresses());
        break;
    }
    if (params.deviceInfo != null) {
      DeviceInfo deviceInfo = params.deviceInfo;
      if (deviceInfo.numVolumes != null && !cloud.equals(Common.CloudType.onprem)) {
        expectedCommand.add("--num_volumes");
        expectedCommand.add(Integer.toString(deviceInfo.numVolumes));
      } else if (deviceInfo.mountPoints != null) {
        expectedCommand.add("--mount_points");
        expectedCommand.add(fakeMountPaths);
      }
      if (deviceInfo.volumeSize != null) {
        expectedCommand.add("--volume_size");
        expectedCommand.add(Integer.toString(deviceInfo.volumeSize));
      }
      if (type == NodeManager.NodeCommandType.Provision && deviceInfo.storageType != null) {
        expectedCommand.add("--volume_type");
        expectedCommand.add(deviceInfo.storageType.toString().toLowerCase());
        if (deviceInfo.storageType == PublicCloudConstants.StorageType.IO1 && deviceInfo.diskIops != null) {
          expectedCommand.add("--disk_iops");
          expectedCommand.add(Integer.toString(deviceInfo.diskIops));
        }
      }

      String packagePath = mockAppConfig.getString("yb.thirdparty.packagePath");
      if (type == NodeManager.NodeCommandType.Provision && packagePath != null) {
        expectedCommand.add("--local_package_path");
        expectedCommand.add(packagePath);
      }
    }

    expectedCommand.add(params.nodeName);
    return expectedCommand;
  }

  @Test
  public void testProvisionNodeCommand() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithoutAssignPublicIP() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
              ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.assignPublicIP = false;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));
      if (t.cloudType.equals(Common.CloudType.aws)) {
        Predicate<String> stringPredicate = p -> p.equals("--assign_public_ip");
        expectedCommand.removeIf(stringPredicate);
      }
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithLocalPackage() {
    String packagePath = "/tmp/third-party";
    new File(packagePath).mkdir();
    when(mockAppConfig.getString("yb.thirdparty.packagePath")).thenReturn(packagePath);

    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }

    File file = new File(packagePath);
    file.delete();
  }

  @Test
  public void testProvisionUseTimeSync() {
    int iteration = 0;
    for (TestData t : testData) {
      for (boolean useTimeSync : ImmutableList.of(true, false)) {
        // Bump up the iteration, for use in the verify call and getting the correct capture.
        ++iteration;
        AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
        buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
            ApiUtils.mockUniverseUpdater(t.cloudType)));
        addValidDeviceInfo(t, params);
        params.useTimeSync = useTimeSync;

        ArgumentCaptor<List> arg = ArgumentCaptor.forClass(List.class);
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
        verify(shellProcessHandler, times(iteration)).run(arg.capture(), any());
        // For AWS and useTimeSync knob set to true, we want to find the flag.
        List<String> cmdArgs = arg.getAllValues().get(iteration - 1);
        assertNotNull(cmdArgs);
        assertTrue(
            cmdArgs.contains("--use_chrony") ==
            (t.cloudType.equals(Common.CloudType.aws) && useTimeSync));
      }
    }
  }

  @Test
  public void testProvisionWithAWSTags() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID,ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      addValidDeviceInfo(t, params);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  private void setInstanceTags(NodeTaskParams params) {
    UserIntent userIntent = new UserIntent();
    userIntent.instanceTags = ImmutableMap.of("Cust", "Test");
    params.clusters.add(new Cluster(ClusterType.PRIMARY, userIntent));
  }

  private void runAndTestProvisionWithAccessKeyAndSG(String sgId) {
    for (TestData t : testData) {
      t.region.setSecurityGroupId(sgId);
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      keyInfo.airGapInstall = true;
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<UUID>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 5;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyIndexOffset += 2;
        if (params.deviceInfo.storageType.equals(PublicCloudConstants.StorageType.IO1)) {
          accessKeyIndexOffset += 2;
        }
      }
      List<String> expectedCommand = new ArrayList<>(t.baseCommand);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t));
      List<String> accessKeyCommand = new ArrayList<String>(ImmutableList.of("--vars_file", "/path/to/vault_file",
          "--vault_password_file", "/path/to/vault_password", "--private_key_file",
          "/path/to/private.key"));
      if (t.cloudType.equals(Common.CloudType.onprem)) {
        accessKeyCommand.add("--air_gap");
      }
      expectedCommand.addAll(expectedCommand.size() - accessKeyIndexOffset, accessKeyCommand);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        List<String> awsAccessKeyCommands = new ArrayList<>();
        awsAccessKeyCommands.add("--key_pair_name");
        awsAccessKeyCommands.add(userIntent.accessKeyCode);
        String customSecurityGroupId = t.region.getSecurityGroupId();
        if (customSecurityGroupId != null) {
          awsAccessKeyCommands.add("--security_group_id");
          awsAccessKeyCommands.add(customSecurityGroupId);
        }
        expectedCommand.addAll(expectedCommand.size() - accessKeyIndexOffset, awsAccessKeyCommands);
      }

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithAccessKeyNoSG() {
    String sgId = "custom_sg_id";
    runAndTestProvisionWithAccessKeyAndSG(sgId);
  }

  @Test
  public void testProvisionNodeCommandWithAccessKeyCustomSG() {
    String sgId = null;
    runAndTestProvisionWithAccessKeyAndSG(sgId);
  }

  @Test
  public void testProvisionNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, createInvalidParams(t));
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleSetupServer.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, createInvalidParams(t));
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testConfigureNodeCommandWithoutReleasePackage() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.2";
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(),
            is("Unable to fetch yugabyte release for version: 0.0.2")));
      }
    }
  }

  @Test
  public void testConfigureNodeCommandWithAccessKey() {
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<UUID>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";

      // Set up expected command
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      List<String> accessKeyCommand = ImmutableList.of(
          "--vars_file", "/path/to/vault_file", "--vault_password_file", "/path/to/vault_password",
          "--private_key_file", "/path/to/private.key");
      expectedCommand.addAll(expectedCommand.size() - 5, accessKeyCommand);

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.1";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithInvalidProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      for (UniverseDefinitionTaskBase.ServerType type : UniverseDefinitionTaskBase.ServerType.values()) {
        try {
            // master and tserver are valid process types.
            if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
              continue;
            }
            params.setProperty("processType", type.toString());
            nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
            fail();
        } catch (RuntimeException re) {
          assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutTaskType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
      }
    }
  }


  @Test
  public void testSoftwareUpgradeWithDownloadNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Download.toString());
      params.setProperty("processType", MASTER.toString());
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.setProperty("processType", MASTER.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutReleasePackage() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.2";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.setProperty("processType", MASTER.toString());
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(),
            is("Unable to fetch yugabyte release for version: 0.0.2")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeNullProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeInvalidProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      for (UniverseDefinitionTaskBase.ServerType type : UniverseDefinitionTaskBase.ServerType.values()) {
        try {
          // master and tserver are valid process types.
          if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
            continue;
          }
          params.setProperty("processType", type.toString());
          nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
          fail();
        } catch (RuntimeException re) {
          assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testGFlagsUpgradeWithEmptyGFlagsNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      params.type = GFlags;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), containsString("GFlags data provided")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeForMasterNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;
      params.setProperty("processType", MASTER.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testEnableYSQLNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
        ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableYSQL = true;
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
        t.region.provider.getConfig());
    }
  }

  @Test
  public void testEnableNodeToNodeTLSNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableNodeToNodeEncrypt = true;
      params.rootCA = createUniverseWithCert(t, params);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
        t.region.provider.getConfig());
    }
  }

  @Test
  public void testEnableClientToNodeTLSNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
        ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableClientToNodeEncrypt = true;
      params.rootCA = createUniverseWithCert(t, params);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
        t.region.provider.getConfig());
    }
  }

  @Test
  public void testEnableAllTLSNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
        ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.enableNodeToNodeEncrypt = true;
      params.enableClientToNodeEncrypt = true;
      params.rootCA = createUniverseWithCert(t, params);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
        t.region.provider.getConfig());
    }
  }

  @Test
  public void testGlobalDefaultCallhome() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
              ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      params.type = Everything;
      params.ybSoftwareVersion = "0.0.1";
      params.callhomeLevel = CallHomeManager.CollectionLevel.valueOf("NONE");
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
              t.region.provider.getConfig());
    }
  }


  @Test
  public void testDestroyNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, createInvalidParams(t));
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
      }
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    for (TestData t : testData) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      buildValidParams(t, params, createUniverse());
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Destroy, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testListNodeCommand() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testControlNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, createInvalidParams(t));
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
      }
    }
  }

  @Test
  public void testControlNodeCommand() {
    for (TestData t : testData) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.process = "master";
      params.command = "create";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Control, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testDockerNodeCommandWithoutDockerNetwork() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(t, params, createUniverse());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      } catch (RuntimeException re) {
        if (t.cloudType == Common.CloudType.docker) {
          assertThat(
              re.getMessage(), allOf(notNullValue(), is("yb.docker.network is not set in application.conf")));
        }
      }
    }
  }

  @Test
  public void testDockerNodeCommandWithDockerNetwork() {
    when(mockAppConfig.getString("yb.docker.network")).thenReturn(DOCKER_NETWORK);

    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSetInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID,ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testRemoveInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID,ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        ApiUtils.insertInstanceTags(univUUID);
        setInstanceTags(params);
        params.deleteTags = "Remove,Also";
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testEmptyInstanceTags() {
    for (TestData t : testData) {
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID,ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Tags, params, t));
      try {
          nodeManager.nodeCommand(NodeManager.NodeCommandType.Tags, params);
          assertNotEquals(t.cloudType, Common.CloudType.aws);
        } catch (RuntimeException re) {
          if (t.cloudType == Common.CloudType.aws) {
            assertThat(
                re.getMessage(), allOf(notNullValue(), is("Invalid instance tags")));
          }
        }
    }
  }

  @Test
  public void testInitYSQL() {
    for (TestData t : testData) {
      List<String> expectedCommand = t.baseCommand;
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID,ApiUtils.mockUniverseUpdater(t.cloudType));
      buildValidParams(t, params, universe);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.InitYSQL, params, t));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.InitYSQL, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand,
        t.region.provider.getConfig());
    }
  }

  @Test
  public void testNegativeInitYSQL() {
    for (TestData t : testData) {
      List<String> expectedCommand = t.baseCommand;
      InstanceActions.Params params = new InstanceActions.Params();
      UUID univUUID = createUniverse().universeUUID;
      Universe universe = Universe.saveDetails(univUUID, ApiUtils.mockUniverseUpdaterWith1TServer0Masters());
      buildValidParams(t, params, universe);
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.InitYSQL, params, t));
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.InitYSQL, params);
      } catch (RuntimeException re) {
        assertThat(
          re.getMessage(), is("Can't run initdb script: No masters found"));
      }
    }
  }
}
