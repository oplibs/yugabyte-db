// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col, ButtonGroup } from 'react-bootstrap';
import RegionMap from './RegionMap';
import NodeDetails from './NodeDetails';
import UniverseInfoPanel from './UniverseInfoPanel';
import ConnectStringPanel from './ConnectStringPanel';
import GraphPanelContainer from '../containers/GraphPanelContainer';
import UniverseModalContainer from '../containers/UniverseModalContainer';
import DeleteUniverseContainer from '../containers/DeleteUniverseContainer';
import TaskProgessPanelContainer from '../containers/TaskProgressPanelContainer.js';

export default class UniverseDetail extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetUniverseTasks();
  }

  componentDidMount() {
    var uuid ;
    if (typeof this.props.universeSelectionId !== "undefined") {
      uuid = this.props.universeUUID;
    } else {
      uuid = this.props.uuid;
    }
    this.props.getUniverseInfo(uuid);
    this.props.fetchUniverseTasks(uuid);
  }

  render() {
    const { currentUniverse, loading } = this.props.universe;
    if (loading) {
      return <div className="container">Loading...</div>;
    } else if (!currentUniverse) {
      return <span />;
    }
    const { universeDetails } = currentUniverse;

    return (
      <Grid id="page-wrapper">
        <Row className="header-row">
          <Col lg={9} >
            <h3>Universe { currentUniverse.name }</h3>
          </Col>
          <ButtonGroup>
            <UniverseModalContainer type="Edit" />
            <DeleteUniverseContainer uuid={this.props.universe.currentUniverse.universeUUID} />
          </ButtonGroup>
        </Row>
        <Row>
          <Col lg={11}>
            <ConnectStringPanel universeId={this.props.universe.currentUniverse.universeUUID}
                                customerId={localStorage.getItem("customer_id")} />
          </Col>
        </Row>
        <Row>
          <Col md={6}>
            <RegionMap regions={currentUniverse.regions}/>
          </Col>
          <Col md={5} lg={5}>
            <UniverseInfoPanel universeInfo={currentUniverse} />
            <TaskProgessPanelContainer />
          </Col>
        </Row>
        <Row>
          <Col lg={11}>
            <NodeDetails nodeDetails={universeDetails.nodeDetailsMap}/>
          </Col>
        </Row>
        <Row>
          <Col lg={11}>
            <GraphPanelContainer nodePrefix={universeDetails.nodePrefix} />
          </Col>
        </Row>
      </Grid>
    );
  }
}
