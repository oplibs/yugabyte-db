// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { Dropdown, MenuItem, FormControl} from 'react-bootstrap';
import { DateTimePicker } from 'react-widgets';
import { withRouter, browserHistory } from 'react-router';
import _ from 'lodash';

import { YBButton } from '../../common/forms/fields';
import { YBPanelItem } from '../../panels';
import { FlexContainer, FlexGrow } from '../../common/flexbox/YBFlexBox';
import { getPromiseState } from 'utils/PromiseUtils';
import { isValidObject, isNonEmptyObject } from 'utils/ObjectUtils';
import './GraphPanelHeader.scss';

require('react-widgets/dist/css/react-widgets.css');
const moment = require('moment');
const momentLocalizer = require('react-widgets/lib/localizers/moment');

// We can define different filter types here, the type parameter should be
// valid type that moment supports except for custom and divider.
// if the filter type has a divider, we would just add a divider in the dropdown
// and custom filter would show custom date picker
const filterTypes = [
  {label: "Last 1 hr", type: "hours", value: "1"},
  {label: "Last 6 hrs", type: "hours", value: "6"},
  {label: "Last 12 hrs", type: "hours", value: "12"},
  {label: "Last 24 hrs", type: "hours", value: "24"},
  {label: "Last 7 days", type: "days", value: "7"},
  {type: "divider"},
  {label: "Custom", type: "custom"}
];

const DEFAULT_FILTER_KEY = 0;
export const DEFAULT_GRAPH_FILTER = {
  startMoment: moment().subtract(
    filterTypes[DEFAULT_FILTER_KEY].value,
    filterTypes[DEFAULT_FILTER_KEY].type),
  endMoment: moment(),
  nodePrefix: "all",
  nodeName: "all",
  filterLabel: filterTypes[DEFAULT_FILTER_KEY].label,
  filterType: filterTypes[DEFAULT_FILTER_KEY].type,
  filterValue: filterTypes[DEFAULT_FILTER_KEY].value
};


class GraphPanelHeader extends Component {
  constructor(props) {
    super(props);
    momentLocalizer(moment);
    this.handleFilterChange = this.handleFilterChange.bind(this);
    this.handleStartDateChange = this.handleStartDateChange.bind(this);
    this.handleEndDateChange = this.handleEndDateChange.bind(this);
    this.applyCustomFilter = this.applyCustomFilter.bind(this);
    this.updateGraphQueryParams = this.updateGraphQueryParams.bind(this);
    const defaultFilter = filterTypes[DEFAULT_FILTER_KEY];
    this.universeItemChanged = this.universeItemChanged.bind(this);
    this.nodeItemChanged = this.nodeItemChanged.bind(this);
    this.submitGraphFilters = this.submitGraphFilters.bind(this);
    this.refreshGraphQuery = this.refreshGraphQuery.bind(this);
    let currentUniverse = "all";
    let currentUniversePrefix  = "all";
    if (this.props.origin === "universe") {
      currentUniverse = this.props.universe.currentUniverse.data;
      currentUniversePrefix = currentUniverse.universeDetails.nodePrefix;
    }
    this.updateUrlQueryParams = this.updateUrlQueryParams.bind(this);
    this.state = {
      showDatePicker: false,
      filterLabel: defaultFilter.label,
      filterType: defaultFilter.type,
      filterValue: defaultFilter.value,
      currentSelectedUniverse: currentUniverse,
      endMoment: moment(),
      startMoment: moment().subtract("1", "hours"),
      nodePrefix: currentUniversePrefix,
      nodeName: "all"
    };
  }

  componentWillMount() {
    const location = browserHistory.getCurrentLocation();
    const currentQuery = location.query;
    let currentFilters = this.state;
    if (isValidObject(currentQuery) && Object.keys(currentQuery).length > 1) {
      const filterParams = {
        nodePrefix: currentQuery.nodePrefix,
        nodeName: currentQuery.nodeName,
        filterType: currentQuery.filterType,
        filterValue: currentQuery.filterValue
      };
      if (currentQuery.filterType === "custom") {
        filterParams.startMoment = moment.unix(currentQuery.startDate);
        filterParams.endMoment = moment.unix(currentQuery.endDate);
        filterParams.filterValue  = "";
        filterParams.filterLabel = "Custom";
      } else {
        const currentFilterItem = filterTypes.find(filterType => filterType.type === currentQuery.filterType
                                                 && filterType.value === currentQuery.filterValue);
        filterParams.filterLabel = currentFilterItem.label;
        filterParams.endMoment = moment();
        filterParams.startMoment = moment().subtract(currentFilterItem.value, currentFilterItem.type);
      }
      this.setState({...filterParams});
      currentFilters = filterParams;
    }
    this.props.changeGraphQueryFilters(currentFilters);
  }

  componentWillReceiveProps(nextProps) {
    const {location, universe, universe: {universeList}} = nextProps;
    if (this.props.location !== nextProps.location ||
       (getPromiseState(universeList).isSuccess() && getPromiseState(this.props.universe.universeList).isLoading())) {
      let nodePrefix = this.state.nodePrefix;
      if (location.query.nodePrefix) {
        nodePrefix = location.query.nodePrefix;
      }
      let currentUniverse;
      if (getPromiseState(universe.currentUniverse).isEmpty() || getPromiseState(universe.currentUniverse).isInit()) {
        currentUniverse = universeList.data.find(function (item) {
          return item.universeDetails.nodePrefix === nodePrefix;
        });
        if (!isNonEmptyObject(currentUniverse)) {
          currentUniverse = "all";
        }
      } else {
        currentUniverse = universe.currentUniverse.data;
      }
      let currentSelectedNode = "all";
      if (isValidObject(location.query.nodeName)) {
        currentSelectedNode = location.query.nodeName;
      }
      this.setState({currentSelectedUniverse: currentUniverse, currentSelectedNode: currentSelectedNode});
    }
  }

  submitGraphFilters(type, val) {
    const queryObject = this.state.filterParams;
    queryObject[type] = val;
    this.props.changeGraphQueryFilters(queryObject);
    this.updateUrlQueryParams(queryObject);
  }

  refreshGraphQuery() {
    const newParams = this.state;
    if (newParams.filterLabel !== "Custom") {
      newParams.startMoment = moment().subtract(newParams.filterValue, newParams.filterType);
      newParams.endMoment = moment();
      this.props.changeGraphQueryFilters(newParams);
    }
  }

  handleFilterChange(eventKey, event) {
    const filterInfo = filterTypes[eventKey] || filterTypes[DEFAULT_FILTER_KEY];
    const newParams = this.state;
    newParams.filterLabel = filterInfo.label;
    newParams.filterType = filterInfo.type;
    newParams.filterValue = filterInfo.value;
    this.setState({
      filterLabel: filterInfo.label,
      filterType: filterInfo.type,
      filterValue: filterInfo.value});

    if (event.target.getAttribute("data-filter-type") !== "custom") {
      const endMoment = moment();
      const startMoment = moment().subtract(filterInfo.value, filterInfo.type);
      newParams.startMoment = startMoment;
      newParams.endMoment = endMoment;
      this.setState({startMoment: startMoment, endMoment: endMoment});
      this.props.changeGraphQueryFilters(newParams);
      this.updateUrlQueryParams(newParams);
    }
  }

  universeItemChanged(event) {
    const {universe: {universeList}} = this.props;
    const self = this;
    let universeFound = false;
    const newParams = this.state;
    for (let counter = 0; counter < universeList.data.length; counter++) {
      if (universeList.data[counter].universeUUID === event.target.value) {
        universeFound = true;
        self.setState({currentSelectedUniverse: universeList[counter], nodePrefix: universeList.data[counter].universeDetails.nodePrefix,
          nodeName: "all"});
        newParams.nodePrefix = universeList.data[counter].universeDetails.nodePrefix;
        break;
      }
    }
    if (!universeFound) {
      self.setState({nodeName: "all", nodePrefix: "all"});
    }
    newParams.nodeName = "all";
    this.props.changeGraphQueryFilters(newParams);
    this.updateUrlQueryParams(newParams);
  }

  nodeItemChanged(event) {
    const newParams = this.state;
    newParams.nodeName = event.target.value;
    this.props.changeGraphQueryFilters(newParams);
    this.setState({nodeName: event.target.value});
    this.updateUrlQueryParams(newParams);
  }

  handleStartDateChange(dateStr) {
    this.setState({startMoment: moment(dateStr)});
  }

  handleEndDateChange(dateStr) {
    this.setState({endMoment: moment(dateStr)});
  }

  applyCustomFilter() {
    this.updateGraphQueryParams(this.state.startMoment, this.state.endMoment);
  }

  updateGraphQueryParams(startMoment, endMoment) {
    const newFilterParams = this.state;
    if (isValidObject(startMoment) && isValidObject(endMoment)) {
      newFilterParams.startMoment = startMoment;
      newFilterParams.endMoment = endMoment;
      this.props.changeGraphQueryFilters(this.state);
      this.updateUrlQueryParams(newFilterParams);
    }
  }

  updateUrlQueryParams(filterParams) {
    const location = Object.assign({}, browserHistory.getCurrentLocation());
    const queryParams = location.query;
    queryParams.nodePrefix = filterParams.nodePrefix;
    queryParams.nodeName = filterParams.nodeName;
    queryParams.filterType = filterParams.filterType;
    queryParams.filterValue = filterParams.filterValue;
    if (queryParams.filterType === "custom") {
      queryParams.startDate = filterParams.startMoment.unix();
      queryParams.endDate = filterParams.endMoment.unix();
    }
    Object.assign(location.query, queryParams);
    browserHistory.push(location);
    this.props.changeGraphQueryFilters(filterParams);
  }

  render() {
    const { origin } = this.props;
    let datePicker = null;
    if (this.state.filterLabel === "Custom") {
      datePicker = (
        <span className="graph-filter-custom" >
          <DateTimePicker
            value={this.state.startMoment.toDate()}
            onChange={this.handleStartDateChange}
            max={new Date()} />
            &nbsp;&ndash;&nbsp;
          <DateTimePicker
            value={this.state.endMoment.toDate()}
            onChange={this.handleEndDateChange}
            max={new Date()} min={this.state.startMoment.toDate()} />
            &nbsp;
          <YBButton btnIcon={"fa fa-caret-right"} onClick={this.applyCustomFilter} />
        </span>
      );
    }

    const self = this;
    const menuItems = filterTypes.map(function(filter, idx) {
      const key = 'graph-filter-' + idx;
      if (filter.type === "divider") {
        return <MenuItem divider key={key}/>;
      }

      return (
        <MenuItem onSelect={self.handleFilterChange} data-filter-type={filter.type}
          key={key} eventKey={idx} active={filter.label === self.state.filterLabel}>
          {filter.label}
        </MenuItem>
      );
    });

    let universePicker = <span/>;
    if (origin === "customer") {
      universePicker = (
        <UniversePicker {...this.props} universeItemChanged={this.universeItemChanged}
          selectedUniverse={self.state.currentSelectedUniverse} />
      );
    }
    return (
      <YBPanelItem
        className="graph-panel"
        header={
          <FlexContainer>

            <h2 className="content-title">Metrics</h2>
            <FlexGrow power={1}>
              <div className="filter-container">
                {universePicker}
                <NodePicker {...this.props} nodeItemChanged={this.nodeItemChanged}
                            selectedUniverse={this.state.currentSelectedUniverse}
                            selectedNode={this.state.currentSelectedNode}/>
                <YBButton btnIcon="fa fa-refresh" btnClass="btn btn-default refresh-btn" onClick={this.refreshGraphQuery}/>
              </div>
            </FlexGrow>
            <FlexGrow>
              <form name="GraphPanelFilterForm">
                <div id="reportrange" className="pull-right">
                  {datePicker}
                  <Dropdown id="graph-filter-dropdown" pullRight={true} >
                    <Dropdown.Toggle>
                      <i className="fa fa-clock-o"></i>&nbsp;
                      {this.state.filterLabel}
                    </Dropdown.Toggle>
                    <Dropdown.Menu>
                      {menuItems}
                    </Dropdown.Menu>
                  </Dropdown>
                </div>
              </form>
            </FlexGrow>
          </FlexContainer>
        }
        body={
          this.props.children
        }
        noBackground
      />
    );
  }
}

export default withRouter(GraphPanelHeader);

class UniversePicker extends Component {
  render() {
    const {universeItemChanged, universe: {universeList}, selectedUniverse} = this.props;
    const universeItems = universeList.data.sort((a, b) => a.name.toLowerCase() > b.name.toLowerCase())
                          .map(function(item, idx){
                            return <option key={idx} value={item.universeUUID} name={item.name}>{item.name}</option>;
                          });
    const universeOptionArray = [<option key={-1} value="all">All</option>].concat(universeItems);
    let currentUniverseValue = "all";
    if (!_.isString(selectedUniverse)) {
      currentUniverseValue = selectedUniverse.universeUUID;
    }
    return (
      <div className="universe-picker">
        Universe:
        <FormControl componentClass="select" placeholder="select" onChange={universeItemChanged} value={currentUniverseValue}>
          {universeOptionArray}
        </FormControl>
      </div>
    );
  }
}

class NodePicker extends Component {
  render() {
    const {selectedUniverse, nodeItemChanged, selectedNode} = this.props;
    let nodeItems =[];
    if (isNonEmptyObject(selectedUniverse) && selectedUniverse!== "all") {
      nodeItems = selectedUniverse.universeDetails.nodeDetailsSet
        .sort((a, b) => a.nodeName.toLowerCase() > b.nodeName.toLowerCase())
        .map((nodeItem, nodeIdx) => (
          <option key={nodeIdx} value={nodeItem.nodeName}>
            {nodeItem.nodeName}
          </option>
        ));
    }
    const nodeOptionArray=[<option key={-1} value="all">All</option>].concat(nodeItems);
    return (
      <div className="node-picker">
        Node:
        <FormControl componentClass="select" onChange={nodeItemChanged} value={selectedNode}>
          {nodeOptionArray}
        </FormControl>
      </div>
    );
  }
}
