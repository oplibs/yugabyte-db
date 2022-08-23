// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import _ from 'lodash';
import { withRouter } from 'react-router';
import { NodeDetailsTable } from '../../universes';
import {
  isNonEmptyArray,
  isDefinedNotNull,
  insertSpacesFromCamelCase,
  isNonEmptyObject,
  isNonEmptyString
} from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import {
  getPrimaryCluster,
  getReadOnlyCluster,
  nodeComparisonFunction
} from '../../../utils/UniverseUtils';
import { hasLiveNodes } from '../../../utils/UniverseUtils';
import { YBLoading } from '../../common/indicators';

const nodeActionExpectedResult = {
  START: 'Live',
  STOP: 'Stopped',
  REMOVE: 'Removed',
  RELEASE: 'Unreachable',
  DELETE: 'Unreachable',
  ADD: 'Live',
  DEFAULT: 'Unreachable'
};
const DEFAULT = "DEFAULT";
class NodeDetails extends Component {
  componentDidMount() {
    const {
      universe: { 
        currentUniverse, 
      },
      selectedNodeName
    } = this.props;
    if (getPromiseState(currentUniverse).isSuccess()) {
      console.log('ENNA DA IDHU123');
      const uuid = currentUniverse.data.universeUUID;
      this.props.getUniversePerNodeStatus(uuid);
      this.props.getMasterLeader(uuid);
      if (hasLiveNodes(currentUniverse.data)) {
        console.log('hello');
        this.props.getUniversePerNodeMetrics(uuid);
      }
      
      if (selectedNodeName) {
        this.props.selectedNodeAllowedActions(uuid, selectedNodeName);
      }

      const universeDetails = currentUniverse.data.universeDetails;
      const primaryCluster = getPrimaryCluster(universeDetails.clusters);
      if (isDefinedNotNull(primaryCluster)) {
        const primaryClusterProvider = primaryCluster.userIntent.provider;
        this.props.fetchNodeListByProvider(primaryClusterProvider);
      }

      const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
      if (isDefinedNotNull(readOnlyCluster)) {
        const readOnlyClusterProvider = readOnlyCluster.userIntent.provider;
        this.props.fetchNodeListByReplicaProvider(readOnlyClusterProvider);
      }
    }
  }

  componentWillUnmount() {
    this.props.resetMasterLeader();
  }

  render() {
  
    const {
      universe: {
        currentUniverse,
        nodeInstanceList,
        replicaNodeInstanceList,
        universePerNodeStatus,
        universePerNodeAllowedActions,
        universePerNodeMetrics,
        universeMasterLeader
      },
      customer,
      providers,
      selectedNodeName,
      selectedNodeAction
    } = this.props;
    const universeDetails = currentUniverse.data.universeDetails;
    const nodeDetails = universeDetails.nodeDetailsSet;
    if (!isNonEmptyArray(nodeDetails)) {
      return <YBLoading />;
    }
    const isReadOnlyUniverse =
      getPromiseState(currentUniverse).isSuccess() &&
      currentUniverse.data.universeDetails.capability === 'READ_ONLY';

    const universeCreated = universeDetails.updateInProgress;
    const sortedNodeDetails = nodeDetails.sort((a, b) =>
      nodeComparisonFunction(a, b, currentUniverse.data.universeDetails.clusters)
    );

    const nodesMetrics =
      getPromiseState(universePerNodeMetrics).isSuccess() &&
      isNonEmptyObject(universePerNodeMetrics.data) &&
      Object.assign({}, ...Object.values(universePerNodeMetrics.data));

    const nodeDetailRows = sortedNodeDetails.map((nodeDetail) => {
      let nodeStatus = '-';
      let masterAlive = false;
      let tserverAlive = false;
      let isLoading = universeCreated;
      let isActionsDisabled = false;
      let allowedNodeActions  = nodeDetail.allowedActions;

      if (
        getPromiseState(universePerNodeStatus).isSuccess() &&
        isNonEmptyObject(universePerNodeStatus.data) &&
        isNonEmptyObject(universePerNodeStatus.data[nodeDetail.nodeName])
      ) {
        nodeStatus = insertSpacesFromCamelCase(
          universePerNodeStatus.data[nodeDetail.nodeName]['node_status']
        );

        masterAlive = universePerNodeStatus.data[nodeDetail.nodeName]['master_alive'];
        tserverAlive = universePerNodeStatus.data[nodeDetail.nodeName]['tserver_alive'];

        isLoading = false;
      }

     
      if (selectedNodeName && selectedNodeName === nodeDetail.nodeName) {
        isActionsDisabled = true;
        console.log('selectedNodeName is success', getPromiseState(universePerNodeAllowedActions).isSuccess());
        console.log('poll node actions', universePerNodeAllowedActions?.data);
        console.log('nodeStatus', nodeStatus);
        console.log('nodeActionExpectedResult', nodeActionExpectedResult[selectedNodeAction]);
        if (getPromiseState(universePerNodeAllowedActions).isSuccess() &&
          isNonEmptyArray(universePerNodeAllowedActions.data) &&
          nodeStatus === nodeActionExpectedResult[selectedNodeAction]
        ) {
            if (selectedNodeAction === "DELETE") {
              window.location.reload();
            }
            isActionsDisabled = false;
            allowedNodeActions = universePerNodeAllowedActions.data;
        }
      }
      
      
      let instanceName = '';
      const nodeName = nodeDetail.nodeName;

      if (isDefinedNotNull(nodeInstanceList)) {
        const matchingInstance = nodeInstanceList.data.filter(
          (instance) => instance.nodeName === nodeName
        );
        instanceName = _.get(matchingInstance, '[0]details.instanceName', '');
      }

      if (!isNonEmptyString(instanceName) && isDefinedNotNull(replicaNodeInstanceList)) {
        const matchingInstance = replicaNodeInstanceList.data.filter(
          (instance) => instance.nodeName === nodeName
        );
        instanceName = _.get(matchingInstance, '[0]details.instanceName', '');
      }

      const isMasterLeader =
        nodeDetail.isMaster &&
        isDefinedNotNull(universeMasterLeader) &&
        getPromiseState(universeMasterLeader).isSuccess() &&
        universeMasterLeader.data.privateIP === nodeDetail.cloudInfo.private_ip;
      const metricsData = nodesMetrics
        ? nodesMetrics[`${nodeDetail.cloudInfo.private_ip}:${nodeDetail.tserverHttpPort}`]
        : {
            active_tablets: null,
            num_sst_files: null,
            ram_used: null,
            read_ops_per_sec: null,
            system_tablets_leaders: null,
            system_tablets_total: null,
            time_since_hb: null,
            total_sst_file_size: null,
            uncompressed_sst_file_size: null,
            uptime_seconds: null,
            user_tablets_leaders: null,
            user_tablets_total: null,
            write_ops_per_sec: null
          };
      return {
        nodeIdx: nodeDetail.nodeIdx,
        name: nodeName,
        instanceName: instanceName,
        cloudItem: `${nodeDetail.cloudInfo.cloud}`,
        regionItem: `${nodeDetail.cloudInfo.region}`,
        azItem: `${nodeDetail.cloudInfo.az}`,
        isMaster: nodeDetail.isMaster ? 'Details' : '-',
        isMasterLeader: isMasterLeader,
        masterPort: nodeDetail.masterHttpPort,
        tserverPort: nodeDetail.tserverHttpPort,
        isTServer: nodeDetail.isTserver ? 'Details' : '-',
        privateIP: nodeDetail.cloudInfo.private_ip,
        publicIP: nodeDetail.cloudInfo.public_ip,
        nodeStatus: nodeStatus,
        allowedActions: allowedNodeActions,
        cloudInfo: nodeDetail.cloudInfo,
        isLoading: isLoading,
        isActionsDisabled,
        isMasterAlive: masterAlive,
        isTserverAlive: tserverAlive,
        placementUUID: nodeDetail.placementUuid,
        ...metricsData
      };
    });

    const primaryCluster = getPrimaryCluster(universeDetails.clusters);
    if (!isNonEmptyObject(primaryCluster)) {
      return <span />;
    }
    const readOnlyCluster = getReadOnlyCluster(universeDetails.clusters);
    const primaryNodeDetails = nodeDetailRows.filter(
      (nodeDetail) => nodeDetail.placementUUID === primaryCluster.uuid
    );
    const readOnlyNodeDetails = isNonEmptyObject(readOnlyCluster)
      ? nodeDetailRows.filter((nodeDetail) => nodeDetail.placementUUID === readOnlyCluster.uuid)
      : [];

    return (
      <Fragment>
        <NodeDetailsTable
          isKubernetesCluster={primaryCluster.userIntent.providerType === 'kubernetes'}
          isReadOnlyUniverse={isReadOnlyUniverse}
          nodeDetails={primaryNodeDetails}
          providerUUID={primaryCluster.userIntent.provider}
          clusterType="primary"
          customer={customer}
          currentUniverse={currentUniverse}
          providers={providers}
        />
        {readOnlyCluster && (
          <NodeDetailsTable
            isKubernetesCluster={readOnlyCluster.userIntent.providerType === 'kubernetes'}
            isReadOnlyUniverse={isReadOnlyUniverse}
            nodeDetails={readOnlyNodeDetails}
            providerUUID={readOnlyCluster.userIntent.provider}
            clusterType="readonly"
            customer={customer}
            currentUniverse={currentUniverse}
            providers={providers}
          />
        )}
      </Fragment>
    );
  }
}

export default withRouter(NodeDetails);