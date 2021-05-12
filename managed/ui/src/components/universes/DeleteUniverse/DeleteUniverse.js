// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { getPromiseState } from '../../../utils/PromiseUtils';
import 'react-bootstrap-multiselect/css/bootstrap-multiselect.css';
import { browserHistory } from 'react-router';
import { Alert } from 'react-bootstrap';
import { YBModal, YBCheckBox, YBTextInput } from '../../common/forms/fields';
import { isEmptyObject } from '../../../utils/ObjectUtils';
import { getReadOnlyCluster } from '../../../utils/UniverseUtils';

export default class DeleteUniverse extends Component {
  constructor(props) {
    super(props);
    this.state = {
      isForceDelete: false,
      isDeleteBackups: false,
      universeName: false
    };
  }

  toggleForceDelete = () => {
    this.setState({ isForceDelete: !this.state.isForceDelete });
  };

  toggleDeleteBackups = () => {
    this.setState({ isDeleteBackups: !this.state.isDeleteBackups });
  };

  onChangeUniverseName = (value) => {
    this.setState({ universeName: value });
  };

  closeDeleteModal = () => {
    this.props.onHide();
  };

  getModalBody = () => {
    const {
      body,
      universe: {
        currentUniverse: {
          data: {
            name,
            universeDetails
          }
        }
      }
    } = this.props;
    const universePaused = universeDetails?.universePaused;

    return (
      <>
        {universePaused ?
          <>
            Are you sure you want to delete the universe?
            <Alert bsStyle="danger">
              <strong>Note: </strong>Terminating paused universes won't
              delete backup objects. If you want to delete backup objects,
              resume this universe and then delete it.
            </Alert>
          </> :
          <>
            {body}
            <br />
          </>
        }
        <br />
        <label>Enter universe name to confirm delete:</label>
        <YBTextInput
          label="Confirm universe name:"
          placeHolder={name}
          input={{ onChange: this.onChangeUniverseName, onBlur: () => {} }}
        />
      </>
    );
  };

  confirmDelete = () => {
    const {
      type,
      universe: {
        currentUniverse: { data }
      },
      submitDeleteUniverse,
      submitDeleteReadReplica
    } = this.props;
    this.props.onHide();
    if (type === 'primary') {
      submitDeleteUniverse(data.universeUUID, this.state.isForceDelete, this.state.isDeleteBackups);
    } else {
      const cluster = getReadOnlyCluster(data.universeDetails.clusters);
      if (isEmptyObject(cluster)) return;
      submitDeleteReadReplica(cluster.uuid, data.universeUUID, this.state.isForceDelete);
    }
  };

  componentDidUpdate(prevProps) {
    if (
      getPromiseState(prevProps.universe.deleteUniverse).isLoading() &&
      getPromiseState(this.props.universe.deleteUniverse).isSuccess()
    ) {
      this.props.fetchUniverseMetadata();
      browserHistory.push('/universes');
    }
  }

  render() {
    const {
      visible,
      title,
      error,
      onHide,
      universe: {
        currentUniverse: {
          data: {
            name,
            universeDetails
          }
        }
      }
    } = this.props;
    const universePaused = universeDetails?.universePaused;

    return (
      <YBModal
        visible={visible}
        formName={'DeleteUniverseForm'}
        onHide={onHide}
        submitLabel={'Yes'}
        cancelLabel={'No'}
        showCancelButton={true}
        title={title + name}
        onFormSubmit={this.confirmDelete}
        error={error}
        footerAccessory={
          <div className="force-delete">
            <YBCheckBox
              label="Ignore Errors and Force Delete"
              className="footer-accessory"
              input={{ checked: this.state.isForceDelete, onChange: this.toggleForceDelete }}
            />
            <YBCheckBox
              label="Delete Backups"
              className="footer-accessory"
              disabled={universePaused}
              input={{ checked: this.state.isDeleteBackups, onChange: this.toggleDeleteBackups }}
            />
          </div>
        }
        asyncValidating={this.state.universeName !== name}
      >
        {this.getModalBody()}
      </YBModal>
    );
  }
}
