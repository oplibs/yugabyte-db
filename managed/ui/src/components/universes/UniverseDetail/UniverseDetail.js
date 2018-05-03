// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Link, withRouter, browserHistory} from 'react-router';
import { Grid, Row, Col, DropdownButton, MenuItem, Tab } from 'react-bootstrap';
import Measure from 'react-measure';
import { CustomerMetricsPanel } from '../../metrics';
import { TaskProgressContainer, TaskListTable } from '../../tasks';
import { RollingUpgradeFormContainer } from 'components/common/forms';
import { UniverseFormContainer, UniverseStatusContainer, NodeDetailsContainer,
         DeleteUniverseContainer, UniverseAppsModal, UniverseOverviewContainer } from '../../universes';
import { YBButton } from '../../common/forms/fields';
import { YBLabelWithIcon } from '../../common/descriptors';
import { YBTabsPanel } from '../../panels';
import { ListTablesContainer, ListBackupsContainer } from '../../tables';
import { isEmptyObject, isNonEmptyObject, isNonEmptyArray } from '../../../utils/ObjectUtils';
import { getPromiseState } from '../../../utils/PromiseUtils';
import { hasLiveNodes } from 'utils/UniverseUtils';

import { YBLoading, YBErrorIndicator } from '../../common/indicators';
import { mouseTrap } from 'react-mousetrap';
import {TASK_SHORT_TIMEOUT} from '../../tasks/constants';
import './UniverseDetail.scss';

class UniverseDetail extends Component {
  constructor(props) {
    super(props);
    this.state = {
      dimensions: {},
    };
  }

  componentWillUnmount() {
    this.props.resetUniverseInfo();
    this.props.resetUniverseTasks();
    this.props.resetTablesList();
  }

  componentWillMount() {
    this.props.bindShortcut(['ctrl+e', 'e'], this.onEditUniverseButtonClick);

    if (this.props.location.pathname !== "/universes/create") {
      let uuid = this.props.uuid;
      if (typeof this.props.universeSelectionId !== "undefined") {
        uuid = this.props.universeUUID;
      }
      this.props.getUniverseInfo(uuid);
      this.props.fetchUniverseTasks(uuid);
    }
  }

  componentDidUpdate(prevProps, prevState, snapshot) {
    const { universe: { currentUniverse } } = this.props;
    if (getPromiseState(currentUniverse).isSuccess() &&
        !getPromiseState(prevProps.universe.currentUniverse).isSuccess()) {
      if (hasLiveNodes(currentUniverse.data)) {
        this.props.fetchUniverseTables(currentUniverse.data.universeUUID);
      }
    }
  }

  onResize(dimensions) {
    dimensions.width -= 20;
    this.setState({dimensions});
  }

  onEditUniverseButtonClick = () => {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const query = {edit: true};
    Object.assign(location.query, query);
    browserHistory.push(location);
  };

  getUniverseInfo = () => {
    const universeUUID = this.props.universe.currentUniverse.data.universeUUID;
    this.props.getUniverseInfo(universeUUID);
  };

  render() {
    const {
      uuid,
      universe,
      universe: { currentUniverse, showModal, visibleModal },
      location: { query, pathname },
      showSoftwareUpgradesModal,
      showGFlagsModal,
      showDeleteUniverseModal,
      closeModal,
      currentCustomer
    } = this.props;
    if (pathname === "/universes/create") {
      return <UniverseFormContainer type="Create"/>;
    }
    if (getPromiseState(currentUniverse).isLoading() || getPromiseState(currentUniverse).isInit()) {
      return <YBLoading />;
    } else if (isEmptyObject(currentUniverse.data)) {
      return <span />;
    }
    if (isNonEmptyObject(query) && query.edit) {
      return <UniverseFormContainer type="Edit" />;
    }

    if (getPromiseState(currentUniverse).isError()) {
      return <YBErrorIndicator type="universe" uuid={uuid}/>;
    }

    const width = this.state.dimensions.width;
    const nodePrefixes = [currentUniverse.data.universeDetails.nodePrefix];
    const tabElements = [
      <Tab eventKey={"overview"} title="Overview" key="overview-tab" mountOnEnter={true} unmountOnExit={true}>
        <UniverseOverviewContainer width={width} currentUniverse={currentUniverse} customer={currentCustomer} />
      </Tab>,
      <Tab eventKey={"tables"} title="Tables" key="tables-tab" mountOnEnter={true} unmountOnExit={true}>
        <ListTablesContainer/>
      </Tab>,
      <Tab eventKey={"nodes"} title="Nodes" key="nodes-tab" mountOnEnter={true} unmountOnExit={true}>
        <NodeDetailsContainer  />
      </Tab>,
      <Tab eventKey={"metrics"} title="Metrics" key="metrics-tab" mountOnEnter={true} unmountOnExit={true}>
        <div className="universe-detail-content-container">
          <CustomerMetricsPanel origin={"universe"} width={width} nodePrefixes={nodePrefixes} />
        </div>
      </Tab>,
      <Tab eventKey={"tasks"} title="Tasks" key="tasks-tab" mountOnEnter={true} unmountOnExit={true}>
        <UniverseTaskList universe={universe}/>
      </Tab>,
      <Tab eventKey={"backups"} title="Backups" key="backups-tab" mountOnEnter={true} unmountOnExit={true}>
        <ListBackupsContainer currentUniverse={currentUniverse.data} />
      </Tab>
    ];
    const currentBreadCrumb = (
      <div className="detail-label-small">
        <Link to="/universes">
          <YBLabelWithIcon>
            Universes
          </YBLabelWithIcon>
        </Link>
        <Link to={`/universes/${currentUniverse.data.universeUUID}`}>
          <YBLabelWithIcon icon="fa fa-angle-right fa-fw">
            {currentUniverse.data.name}
          </YBLabelWithIcon>
        </Link>
      </div>
    );

    return (
      <Grid id="page-wrapper" fluid={true} className="universe-details universe-details-new">
        <Row>
          <Col lg={10} sm={8} xs={6}>

            {/* UNIVERSE NAME */}
            {currentBreadCrumb}
            <div className="universe-detail-status-container">
              <h2>
                { currentUniverse.data.name }
              </h2>
              <UniverseStatusContainer currentUniverse={currentUniverse.data} showLabelText={true} refreshUniverseData={this.getUniverseInfo}/>
            </div>
          </Col>
          <Col lg={2} sm={4}  xs={6} className="page-action-buttons">

            {/* UNIVERSE EDIT */}
            <div className="universe-detail-btn-group">
              <UniverseAppsModal nodeDetails={currentUniverse.data.universeDetails.nodeDetailsSet}/>
              <YBButton btnClass=" btn btn-orange"
                        btnText="Edit" btnIcon="fa fa-database" onClick={this.onEditUniverseButtonClick} />

              <DropdownButton title="More" id="bg-nested-dropdown" pullRight>
                <MenuItem eventKey="1" onClick={showSoftwareUpgradesModal}>

                  <YBLabelWithIcon icon="fa fa-refresh fa-fw">
                    Upgrade Software
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={showGFlagsModal} >
                  <YBLabelWithIcon icon="fa fa-flag fa-fw">
                    Edit GFlags
                  </YBLabelWithIcon>
                </MenuItem>
                <MenuItem eventKey="2" onClick={showDeleteUniverseModal} >
                  <YBLabelWithIcon icon="fa fa-trash-o fa-fw">
                    Delete Universe
                  </YBLabelWithIcon>
                </MenuItem>
              </DropdownButton>
            </div>
          </Col>
          <RollingUpgradeFormContainer modalVisible={showModal &&
          (visibleModal === "gFlagsModal" || visibleModal ==="softwareUpgradesModal")}
                                       onHide={closeModal} />
          <DeleteUniverseContainer visible={showModal && visibleModal==="deleteUniverseModal"}
                                   onHide={closeModal} title="Delete Universe"/>
        </Row>

        <Measure onMeasure={this.onResize.bind(this)}>
          <YBTabsPanel defaultTab={"overview"} id={"universe-tab-panel"} className="universe-detail">
            { tabElements }
          </YBTabsPanel>
        </Measure>
      </Grid>
    );
  }
}

class UniverseTaskList extends Component {
  render() {
    const {universe: {universeTasks, currentUniverse}} = this.props;
    let universeTaskUUIDs = [];
    const universeTaskHistoryArray = [];
    let universeTaskHistory = <span/>;
    if (getPromiseState(universeTasks).isLoading()) {
      universeTaskHistory = <YBLoading />;
    }
    const currentUniverseTasks = universeTasks.data[currentUniverse.data.universeUUID];
    if (getPromiseState(universeTasks).isSuccess() && isNonEmptyObject(currentUniverse.data) && isNonEmptyArray(currentUniverseTasks)) {
      universeTaskUUIDs = currentUniverseTasks.map(function(task) {
        if (task.status !== "Running") {
          universeTaskHistoryArray.push(task);
        }
        return (task.status !== "Failure" && task.percentComplete !== 100) ? task.id : false;
      }).filter(Boolean);
    }
    if (isNonEmptyArray(universeTaskHistoryArray)) {
      universeTaskHistory = <TaskListTable taskList={universeTaskHistoryArray} title={"Task History"}/>;
    }
    let currentTaskProgress = <span/>;
    if (getPromiseState(universeTasks).isLoading()) {
      currentTaskProgress = <YBLoading/>;
    }
    else if (getPromiseState(universeTasks).isSuccess() && isNonEmptyArray(universeTaskUUIDs)) {
      currentTaskProgress = <TaskProgressContainer taskUUIDs={universeTaskUUIDs} type="StepBar" timeoutInterval={TASK_SHORT_TIMEOUT}/>;
    }
    return (
      <div className="universe-detail-content-container">
        {currentTaskProgress}
        {universeTaskHistory}
      </div>
    );
  }
}

export default withRouter(mouseTrap(UniverseDetail));
