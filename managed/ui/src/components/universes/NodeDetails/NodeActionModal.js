// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { YBModal } from '../../common/forms/fields';
import PropTypes from 'prop-types';
import { browserHistory, withRouter } from 'react-router';
import { NodeAction } from '../../universes';

const nodeActionExpectedResult = {
  START: 'Live',
  STOP: 'Stopped',
  REMOVE: 'Removed',
  RELEASE: 'Unreachable',
  DELETE: 'Unreachable',
};
class NodeActionModal extends Component {
  static propTypes = {
    nodeInfo: PropTypes.object.isRequired,
    actionType: PropTypes.string
  };

  pollNodeStatusUpdate = (universeUUID, actionType, nodeName, payload) => {
    const { preformGetUniversePerNodeStatus,
      preformGetUniversePerNodeStatusResponse,
      selectedNodeAllowedActions
     } = this.props;
    this.interval = setTimeout(() => {
      preformGetUniversePerNodeStatus(universeUUID).then((response) => {
        if (response.payload && response.payload.data) {
          const node = response.payload.data[nodeName];
          console.log('HELLO RAJ');
          if (
            actionType === 'DELETE' ||
            node.node_status === nodeActionExpectedResult[actionType]
          ) {
            clearInterval(this.interval);
            console.log('THE ALLOWED ACTIONS CALL IS MADE');
            selectedNodeAllowedActions(universeUUID, nodeName);
            preformGetUniversePerNodeStatusResponse(response.payload);
            return;
          }
          preformGetUniversePerNodeStatusResponse(response.payload);
          this.pollNodeStatusUpdate(universeUUID, actionType, nodeName, payload);
        }
      },
      );

    }, 1500);
  };

  componentWillUnmount() {
    if (this.interval) {
      clearInterval(this.interval);
    }
  }

  performNodeAction = () => {
    const {
      universe: { currentUniverse },
      nodeInfo,
      actionType,
      performUniverseNodeAction,
      onHide
    } = this.props;
    const universeUUID = currentUniverse.data.universeUUID;
    performUniverseNodeAction(universeUUID, nodeInfo.name, actionType).then((response) => {
      if (response.error !== true) {
        this.pollNodeStatusUpdate(
          universeUUID,
          actionType,
          nodeInfo.name,
          response.payload
        );
      }
    });

    
    onHide();
    this.props.setNodeForSelectedAction(nodeInfo.name, actionType);
    const location = {
      pathname: '/universes/' + universeUUID + '/nodes',
      state: { clickedAction: actionType, nodeName:  nodeInfo.name }
    }
    
    browserHistory.push(location);
    // browserHistory.push('/universes/' + universeUUID + '/nodes');
  };

  render() {
    const { visible, onHide, nodeInfo, actionType } = this.props;
    if (actionType === null || nodeInfo === null) {
      return <span />;
    }

    return (
      <div className="universe-apps-modal">
        <YBModal
          title={`Perform Node Action: ${NodeAction.getCaption(actionType)} `}
          visible={visible}
          onHide={onHide}
          showCancelButton={true}
          cancelLabel={'Cancel'}
          onFormSubmit={this.performNodeAction}
        >
          Are you sure you want to {actionType.toLowerCase()} {nodeInfo.name}?
        </YBModal>
      </div>
    );
  }
}


export default withRouter(NodeActionModal)