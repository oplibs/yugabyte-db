// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link } from 'react-router';
import { Row, Col } from 'react-bootstrap';
import {YBLoadingIcon} from '../../common/indicators';
import { isValidObject, isValidArray, isValidNumber } from 'utils/ObjectUtils';
import { YBCost, DescriptionItem } from 'components/common/descriptors';
import { UniverseFormContainer, UniverseStatusContainer } from 'components/universes';
import './UniverseDisplayPanel.scss';

class CreateUniverseButtonComponent extends Component {
  render() {
    return (
      <Col md={3} lg={2}>
        <div className="create-universe-button" {...this.props}>
          <div className="btn-icon">
            <i className="fa fa-plus"/>
          </div>
          <div className="display-name text-center">
            Create Universe
          </div>
        </div>
      </Col>
    )
  }
}

class UniverseDisplayItem extends Component {
  render() {
    const {universe} = this.props;
    if (!isValidObject(universe)) {
      return <span/>;
    }
    
    var replicationFactor = <span>{`${universe.universeDetails.userIntent.replicationFactor}`}</span>
    var numNodes = <span>{universe.universeDetails.userIntent.numNodes}</span>
    var costPerMonth = <span>n/a</span>;
    if (isValidNumber(universe.pricePerHour)) {
      costPerMonth = <YBCost value={universe.pricePerHour} multiplier={"month"}/>
    }
    return (
      <Col md={3} lg={2}>
        <div className="universe-display-item-container">
          <div className="status-icon">
            <UniverseStatusContainer currentUniverse={universe} />
          </div>
          <div className="display-name">
            <Link to={"/universes/" + universe.universeUUID}>
              {universe.name}
            </Link>
          </div>
          <div className="description-item-list">
            <DescriptionItem title="Replication Factor">
              <span>{replicationFactor}</span>
            </DescriptionItem>
            <DescriptionItem title="Nodes">
              <span>{numNodes}</span>
            </DescriptionItem>
            <DescriptionItem title="Monthly Cost">
              <span>{costPerMonth}</span>
            </DescriptionItem>
          </div>
        </div>
      </Col>
    )
  }
}

export default class UniverseDisplayPanel extends Component {
  render() {
    var self = this;
    const { universe: {universeList, loading, showModal, visibleModal}, cloud} = this.props;
    if (isValidArray(cloud.providers)) {
      var universeDisplayList = universeList.map(function(universeItem, idx){
        return <UniverseDisplayItem key={universeItem.name + idx} universe={universeItem}/>
      });
      var createUniverseButton = <CreateUniverseButtonComponent onClick={() => self.props.showUniverseModal()}/>;
      return (
        <div className="universe-display-panel-container">
          <h2>Universes</h2>
          <Row>
            {universeDisplayList}
            {createUniverseButton}
            <UniverseFormContainer type="Create"
                                   visible={showModal===true && visibleModal==="universeModal"}
                                   onHide={this.props.closeUniverseModal} title="Create Universe" />
          </Row>
        </div>
      )
    } else if (!loading.universeList && cloud.status !== "storage" && cloud.status !== "init") {
      return (
        <div className="get-started-config">
          <span>Welcome to the <div className="yb-data-name">YugaByte Admin Console.</div></span>
          <span>Before you can create a Universe, you must configure a cloud provider.</span>
          <span><Link to="config">Click Here to Configure A Provider</Link></span>
        </div>
      )
    } else {
      return <YBLoadingIcon/>;
    }
  }
}
