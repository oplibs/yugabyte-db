// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.NodeActionType.HARD_REBOOT;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.RebootNodeInUniverse;
import com.yugabyte.yw.commissioner.tasks.params.DetachedNodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.JWTVerifier.ClientType;
import com.yugabyte.yw.controllers.handlers.NodeAgentHandler;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.forms.NodeDetailsResp;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.AllowedActionsHelper;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeConfig.ValidationResult;
import com.yugabyte.yw.models.helpers.NodeConfigValidator;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "Node instances",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class NodeInstanceController extends AuthenticatedController {

  @Inject Commissioner commissioner;

  @Inject NodeAgentHandler nodeAgentHandler;

  @Inject NodeConfigValidator nodeConfigValidator;

  /**
   * GET endpoint for Node data
   *
   * @param customerUuid the customer UUID
   * @param nodeUuid the node UUID
   * @return JSON response with Node data
   */
  @ApiOperation(
      value = "Get a node instance",
      response = NodeInstance.class,
      nickname = "getNodeInstance")
  public Result get(UUID customerUuid, UUID nodeUuid) {
    Customer.getOrBadRequest(customerUuid);
    NodeInstance node = NodeInstance.getOrBadRequest(nodeUuid);
    return PlatformResults.withData(node);
  }

  /**
   * GET endpoint for Node data
   *
   * @param customerUUID the customer UUID
   * @param universeUUID the universe UUID
   * @param nodeName the node name
   * @return JSON response with Node data
   */
  @ApiOperation(
      value = "Get node details",
      response = NodeDetailsResp.class,
      nickname = "getNodeDetails")
  public Result getNodeDetails(UUID customerUUID, UUID universeUUID, String nodeName) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    NodeDetails detail = universe.getNode(nodeName);
    NodeDetailsResp resp = new NodeDetailsResp(detail, universe);
    return PlatformResults.withData(resp);
  }

  /**
   * GET endpoint for getting all unused nodes under a zone
   *
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response with list of nodes
   */
  @ApiOperation(
      value = "List all of a zone's node instances",
      response = NodeInstance.class,
      responseContainer = "List")
  public Result listByZone(UUID customerUuid, UUID zoneUuid) {
    Customer.getOrBadRequest(customerUuid);
    AvailabilityZone.getOrBadRequest(zoneUuid);

    try {
      List<NodeInstance> nodes = NodeInstance.listByZone(zoneUuid, null /* instanceTypeCode */);
      return PlatformResults.withData(nodes);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  @ApiOperation(
      value = "List all of a provider's node instances",
      response = NodeInstance.class,
      responseContainer = "List")
  public Result listByProvider(UUID customerUUID, UUID providerUUID) {
    List<NodeInstance> regionList;
    try {
      regionList = NodeInstance.listByProvider(providerUUID);
    } catch (Exception e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return PlatformResults.withData(regionList);
  }

  /**
   * Validates the node instance.
   *
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return YBSuccess or throws PlatformServiceException with bad request code.
   */
  @ApiOperation(
      value = "Validate a node instance",
      response = ValidationResult.class,
      responseContainer = "Map",
      nickname = "validateNodeInstance")
  @ApiImplicitParam(
      name = "Node instance",
      value = "Node instance data to be validated",
      required = true,
      dataType = "com.yugabyte.yw.forms.NodeInstanceFormData.NodeInstanceData",
      paramType = "body")
  public Result validate(UUID customerUuid, UUID zoneUuid) {
    NodeInstanceData nodeData = parseJsonAndValidate(NodeInstanceData.class);
    Customer.getOrBadRequest(customerUuid);
    AvailabilityZone az = AvailabilityZone.getOrBadRequest(zoneUuid);
    return PlatformResults.withData(
        nodeConfigValidator.validateNodeConfigs(az.getProvider(), nodeData));
  }

  /**
   * POST endpoint for creating new Node(s)
   *
   * @param customerUuid the customer UUID
   * @param zoneUuid the zone UUID
   * @return JSON response of newly created Nodes
   */
  @ApiOperation(
      value = "Create a node instance",
      response = NodeInstance.class,
      responseContainer = "Map",
      nickname = "createNodeInstance")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Node instance",
        value = "Node instance data to be created",
        required = true,
        dataType = "com.yugabyte.yw.forms.NodeInstanceFormData",
        paramType = "body")
  })
  public Result create(UUID customerUuid, UUID zoneUuid) {
    Customer.getOrBadRequest(customerUuid);
    AvailabilityZone az = AvailabilityZone.getOrBadRequest(zoneUuid);
    NodeInstanceFormData nodeInstanceFormData = parseJsonAndValidate(NodeInstanceFormData.class);
    List<NodeInstanceData> nodeDataList = nodeInstanceFormData.nodes;
    Optional<ClientType> clientTypeOp = maybeGetJWTClientType();
    List<String> createdNodeUuids = new ArrayList<String>();
    Provider provider = az.getProvider();
    Map<String, NodeInstance> nodes = new HashMap<>();
    for (NodeInstanceData nodeData : nodeDataList) {
      if (!NodeInstance.checkIpInUse(nodeData.ip)) {
        if (clientTypeOp.isPresent() && clientTypeOp.get() == ClientType.NODE_AGENT) {
          NodeAgent nodeAgent = NodeAgent.getOrBadRequest(customerUuid, getJWTClientUuid());
          nodeAgent.ensureState(State.READY);
          List<ValidationResult> failedResults =
              nodeConfigValidator
                  .validateNodeConfigs(provider, nodeData)
                  .values()
                  .stream()
                  .filter(r -> r.isRequired())
                  .filter(r -> !r.isValid())
                  .collect(Collectors.toList());
          if (CollectionUtils.isNotEmpty(failedResults)) {
            log.error("Failed node configuration types: {}", failedResults);
            throw new PlatformServiceException(
                BAD_REQUEST, "Invalid node configurations: " + failedResults);
          }
        }
        NodeInstance node = NodeInstance.create(zoneUuid, nodeData);
        nodes.put(node.getDetails().ip, node);
        createdNodeUuids.add(node.getNodeUuid().toString());
      }
    }
    if (nodes.size() > 0) {
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.NodeInstance,
              createdNodeUuids.toString(),
              Audit.ActionType.Create,
              Json.toJson(nodeInstanceFormData));
      return PlatformResults.withData(nodes);
    }
    throw new PlatformServiceException(
        BAD_REQUEST, "Invalid nodes in request. Duplicate IP Addresses are not allowed.");
  }

  @ApiOperation(value = "Detached node action", response = YBPTask.class)
  public Result detachedNodeAction(UUID customerUUID, UUID providerUUID, String instanceIP) {
    // Validate customer UUID and universe UUID and AWS provider.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    NodeInstance node = findNodeOrThrow(provider, instanceIP);
    NodeActionFormData nodeActionFormData = parseJsonAndValidate(NodeActionFormData.class);
    NodeActionType nodeAction = nodeActionFormData.getNodeAction();
    if (!nodeAction.isForDetached()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Should provide only detached node action, but found " + nodeAction);
    }
    if (nodeAction == NodeActionType.PRECHECK_DETACHED && node.isInUse()) {
      return Results.status(OK); // Skip checks for node in use
    }
    List<CustomerTask> running = CustomerTask.findIncompleteByTargetUUID(node.getNodeUuid());
    if (!running.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Node " + node.getNodeUuid() + " has incomplete tasks");
    }
    DetachedNodeTaskParams taskParams = new DetachedNodeTaskParams();
    taskParams.setNodeUuid(node.getNodeUuid());
    taskParams.setInstanceType(node.getInstanceTypeCode());
    taskParams.setAzUuid(node.getZoneUuid());

    UUID taskUUID = commissioner.submit(nodeAction.getCommissionerTask(), taskParams);
    CustomerTask.create(
        customer,
        node.getNodeUuid(),
        taskUUID,
        CustomerTask.TargetType.Node,
        nodeAction.getCustomerTask(),
        node.getNodeName());

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.NodeInstance,
            Objects.toString(node.getNodeUuid(), null),
            Audit.ActionType.Create,
            Json.toJson(nodeActionFormData),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(value = "Delete a node instance", response = YBPSuccess.class)
  public Result deleteInstance(UUID customerUUID, UUID providerUUID, String instanceIP) {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    NodeInstance nodeToBeFound = findNodeOrThrow(provider, instanceIP);
    if (nodeToBeFound.isInUse()) {
      throw new PlatformServiceException(BAD_REQUEST, "Node is in use");
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.NodeInstance,
            Objects.toString(nodeToBeFound.getNodeUuid(), null),
            Audit.ActionType.Delete);
    nodeToBeFound.delete();
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Update a node", response = YBPTask.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Node action",
        value = "Node action data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.NodeActionFormData",
        paramType = "body")
  })
  public Result nodeAction(UUID customerUUID, UUID universeUUID, String nodeName) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    universe.getNodeOrBadRequest(nodeName);
    NodeActionFormData nodeActionFormData = parseJsonAndValidate(NodeActionFormData.class);

    if (!universe.getUniverseDetails().isUniverseEditable()) {
      String errMsg = "Node actions cannot be performed on universe UUID " + universeUUID;
      log.error(errMsg);
      return ApiResponse.error(BAD_REQUEST, errMsg);
    }

    NodeActionType nodeAction = nodeActionFormData.getNodeAction();
    NodeTaskParams taskParams = new NodeTaskParams();

    if (nodeAction == NodeActionType.REBOOT || nodeAction == HARD_REBOOT) {
      RebootNodeInUniverse.Params params =
          UniverseControllerRequestBinder.deepCopy(
              universe.getUniverseDetails(), RebootNodeInUniverse.Params.class);
      params.isHardReboot = nodeAction == HARD_REBOOT;
      taskParams = params;
    } else {
      taskParams =
          UniverseControllerRequestBinder.deepCopy(
              universe.getUniverseDetails(), NodeTaskParams.class);
    }

    taskParams.nodeName = nodeName;
    taskParams.creatingUser = CommonUtils.getUserFromContext(ctx());

    // Check deleting/removing a node will not go below the RF
    // TODO: Always check this for all actions?? For now leaving it as is since it breaks many tests
    if (nodeAction == NodeActionType.STOP
        || nodeAction == NodeActionType.REMOVE
        || nodeAction == NodeActionType.DELETE
        || nodeAction == NodeActionType.REBOOT
        || nodeAction == NodeActionType.HARD_REBOOT) {
      // Always check this?? For now leaving it as is since it breaks many tests
      new AllowedActionsHelper(universe, universe.getNode(nodeName))
          .allowedOrBadRequest(nodeAction);
    }
    if (nodeAction == NodeActionType.ADD
        || nodeAction == NodeActionType.START
        || nodeAction == NodeActionType.START_MASTER
        || nodeAction == NodeActionType.STOP) {
      if (!CertificateInfo.isCertificateValid(taskParams.rootCA)) {
        String errMsg =
            String.format(
                "The certificate %s needs info. Update the cert" + " and retry.",
                CertificateInfo.get(taskParams.rootCA).label);
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }

    if (nodeAction == NodeActionType.QUERY) {
      String errMsg = "Node action not allowed for this action type.";
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    log.info(
        "{} Node {} in universe={}: name={} at version={}.",
        nodeAction.toString(false),
        nodeName,
        universe.universeUUID,
        universe.name,
        universe.version);

    UUID taskUUID = commissioner.submit(nodeAction.getCommissionerTask(), taskParams);
    CustomerTask.create(
        customer,
        universe.universeUUID,
        taskUUID,
        CustomerTask.TargetType.Node,
        nodeAction.getCustomerTask(),
        nodeName);
    log.info(
        "Saved task uuid {} in customer tasks table for universe {} : {} for node {}",
        taskUUID,
        universe.universeUUID,
        universe.name,
        nodeName);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.NodeInstance,
            nodeName,
            Audit.ActionType.Update,
            Json.toJson(nodeActionFormData),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  private NodeInstance findNodeOrThrow(Provider provider, String instanceIP) {
    List<NodeInstance> nodesInProvider = NodeInstance.listByProvider(provider.uuid);
    // TODO: Need to convert routes to use UUID instead of instances' IP address
    // See: https://github.com/yugabyte/yugabyte-db/issues/7936
    return nodesInProvider
        .stream()
        .filter(node -> node.getDetails().ip.equals(instanceIP))
        .findFirst()
        .orElseThrow(() -> new PlatformServiceException(BAD_REQUEST, "Node Not Found"));
  }
}
