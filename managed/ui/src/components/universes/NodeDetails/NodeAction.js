// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { NodeActionModalContainer, NodeConnectModal } from '../../universes';
import { DropdownButton, MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { isNonEmptyArray } from 'utils/ObjectUtils';

import _ from 'lodash';

export default class NodeAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false,
      actionType: null
    };
    this.closeModal = this.closeModal.bind(this);
  }

  static propTypes = {
    currentRow: PropTypes.object,
    actionType: PropTypes.oneOf(['STOP', 'REMOVE'])
  };

  openModal(actionType) {
    this.setState((prevState, props) => {
      return {
        selectedRow: props.row,
        actionType: actionType,
        showModal: true
      };
    });
  }

  closeModal() {
    this.setState((prevState, props) => {
      return {
        showModal: false
      };
    });
  }

  getLabel(actionType) {
    let btnLabel = null;
    let btnIcon = null;
    if (actionType === "STOP") {
      btnLabel = "Stop Processes";
      btnIcon = "fa fa-stop-circle";
    } else if (actionType === "REMOVE") {
      btnLabel = "Remove Node";
      btnIcon = "fa fa-minus-circle";
    } else if (actionType === "DELETE") {
      btnLabel = "Delete Node";
      btnIcon = "fa fa-minus-circle";
    } else if (actionType === "RELEASE") {
      btnLabel = "Release Instance";
      btnIcon = "fa fa-trash";
    } else if (actionType === "START") {
      btnLabel = "Start Processes";
      btnIcon = "fa fa-play-circle";
    } else if (actionType === "ADD") {
      btnLabel = "Add Node";
      btnIcon = "fa fa-plus-circle";
    } else if (actionType === "CONNECT") {
      btnLabel = "Connect";
      btnIcon = "fa fa-link";
    }

    return (
      <YBLabelWithIcon icon={btnIcon}>
        {btnLabel}
      </YBLabelWithIcon>
    );
  }

  render() {
    const { currentRow, providerUUID } = this.props;
    if (!isNonEmptyArray(currentRow.allowedActions)) {
      return (
        <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
          <MenuItem disabled>
            No actions available
          </MenuItem>
        </DropdownButton>);
    }
    const actionButtons = currentRow.allowedActions.map((actionType, idx) => {
      const btnId = _.uniqueId('node_action_btn_');
      return (
        <MenuItem key={btnId} eventKey={btnId} onClick={this.openModal.bind(this, actionType)}>
          {this.getLabel(actionType)}
        </MenuItem>
      );
    });

    return (
      <DropdownButton className="btn btn-default" title="Actions" id="bg-nested-dropdown" pullRight>
        <NodeConnectModal currentRow={currentRow} providerUUID={providerUUID} label={this.getLabel("CONNECT")}/>
        {actionButtons}
        <NodeActionModalContainer
          visible = {this.state.showModal}
          onHide = { this.closeModal}
          nodeInfo = {currentRow}
          actionType = {this.state.actionType}
        />
      </DropdownButton>
    );
  }
}
