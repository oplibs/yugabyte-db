// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { Grid, Row, Col } from 'react-bootstrap';
import RegionMap from './RegionMap';
import NodeDetails from './NodeDetails';
import UniverseInfoPanel from './UniverseInfoPanel';
import ConnectStringPanel from './ConnectStringPanel';
import GraphPanelContainer from '../containers/GraphPanelContainer';
import UniverseModalContainer from '../containers/UniverseModalContainer';
import DeleteUniverseContainer from '../containers/DeleteUniverseContainer';

export default class UniverseDetail extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
  }

  componentDidMount() {
    var uuid ;
    if (typeof this.props.universeSelectionId !== "undefined") {
      uuid = this.props.universeUUID;
    } else {
      uuid = this.props.uuid;
    }
    this.props.getUniverseInfo(uuid);
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
          <h3>Universe { currentUniverse.name }</h3>
          <UniverseModalContainer type="Edit" />
          <DeleteUniverseContainer uuid={this.props.universe.currentUniverse.universeUUID} />
        </Row>
        <Row>
          <ConnectStringPanel universeId={this.props.universe.currentUniverse.universeUUID}
                              customerId={localStorage.getItem("customer_id")} />
        </Row>
        <Row>
          <Col md={6}>
            <RegionMap regions={currentUniverse.regions}/>
          </Col>
          <Col md={6}>
            <UniverseInfoPanel universeInfo={currentUniverse} />
          </Col>
        </Row>
        <Row>
          <NodeDetails nodeDetails={universeDetails.nodeDetailsMap}/>
        </Row>
        <Row>
          <GraphPanelContainer nodePrefix={universeDetails.nodePrefix} />
        </Row>
      </Grid>);
  }
}
