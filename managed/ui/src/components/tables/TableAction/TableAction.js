// Copyright (c) YugaByte, Inc.

import React, { Component, Fragment } from 'react';
import PropTypes from 'prop-types';
import { BulkImportContainer, DropTableContainer, CreateBackupContainer, RestoreBackupContainer } from '../../tables';
import {  MenuItem } from 'react-bootstrap';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBButton } from '../../common/forms/fields';

import _ from 'lodash';

export default class TableAction extends Component {
  constructor(props) {
    super(props);
    this.state = {
      showModal: false
    };
    this.openModal = this.openModal.bind(this);
    this.closeModal = this.closeModal.bind(this);
  }

  static propTypes = {
    currentRow: PropTypes.object,
    isMenuItem: PropTypes.bool,
    actionType: PropTypes.oneOf(['drop', 'import', 'create-backup', 'restore-backup'])
  };

  static defaultProps = {
    isMenuItem: true
  };

  openModal() {
    this.setState((prevState, props) => {
      return {
        selectedRow: props.currentRow,
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

  render() {
    const { actionType, isMenuItem } = this.props;
    let modalContainer = null;
    let btnLabel = null;
    let btnIcon = null;
    if (actionType === "drop") {
      btnLabel = "Drop Table";
      btnIcon = "fa fa-trash";
      modalContainer = (<DropTableContainer
        visible = {this.state.showModal}
        onHide = { this.closeModal}
        tableInfo = {this.state.selectedRow}
      />);
    } else if (actionType === "import") {
      btnLabel = "Bulk Import";
      btnIcon = "fa fa-download";
      modalContainer = (<BulkImportContainer
        visible = {this.state.showModal}
        onHide = {this.closeModal}
        tableInfo = {this.state.selectedRow}
      />);
    } else if (actionType === "create-backup") {
      btnLabel = "Create Backup";
      btnIcon = "fa fa-upload";
      modalContainer = (<CreateBackupContainer
        visible={this.state.showModal}
        onHide={this.closeModal}
        tableInfo={this.state.selectedRow}
      />);
    } else if (actionType === "restore-backup") {
      btnLabel = "Restore Backup";
      btnIcon = "fa fa-download";
      modalContainer = (<RestoreBackupContainer
        visible={this.state.showModal}
        onHide={this.closeModal}
        backupInfo={this.state.selectedRow}
      />);
    }

    const btnId = _.uniqueId('table_action_btn_');
    if (isMenuItem) {
      return (
        <Fragment>
          <MenuItem eventKey={btnId} onClick={this.openModal}>
            <YBLabelWithIcon icon={btnIcon}>
              {btnLabel}
            </YBLabelWithIcon>
          </MenuItem>
          {modalContainer}
        </Fragment>
      );
    }
    return (
      <div className={this.props.className}>
        <YBButton btnText={btnLabel} btnIcon={btnIcon}
                btnClass={"btn btn-orange"} onClick={this.openModal} />
        {modalContainer}
      </div>
    );
  }
}
