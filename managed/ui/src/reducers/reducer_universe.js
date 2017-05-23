// Copyright (c) YugaByte, Inc.

import {  FETCH_UNIVERSE_INFO, RESET_UNIVERSE_INFO, FETCH_UNIVERSE_INFO_RESPONSE,
          CREATE_UNIVERSE, CREATE_UNIVERSE_RESPONSE, EDIT_UNIVERSE, EDIT_UNIVERSE_RESPONSE,
          FETCH_UNIVERSE_LIST, FETCH_UNIVERSE_LIST_RESPONSE,
          RESET_UNIVERSE_LIST, DELETE_UNIVERSE, DELETE_UNIVERSE_RESPONSE, FETCH_UNIVERSE_TASKS, FETCH_UNIVERSE_TASKS_SUCCESS,
          FETCH_UNIVERSE_TASKS_FAILURE, RESET_UNIVERSE_TASKS,
          OPEN_DIALOG, CLOSE_DIALOG, CONFIGURE_UNIVERSE_TEMPLATE, CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE, CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS,
          CONFIGURE_UNIVERSE_RESOURCES, CONFIGURE_UNIVERSE_RESOURCES_RESPONSE, ROLLING_UPGRADE, ROLLING_UPGRADE_SUCCESS, ROLLING_UPGRADE_FAILURE,
          RESET_ROLLING_UPGRADE, SET_UNIVERSE_METRICS, SET_PLACEMENT_STATUS, RESET_UNIVERSE_CONFIGURATION, FETCH_UNIVERSE_METADATA }
  from '../actions/universe';
import _ from 'lodash';
import {getInitialState, setLoadingState, setPromiseResponse, setSuccessState } from 'utils/PromiseUtils.js';
import { isNonEmptyArray } from 'utils/ObjectUtils.js';


const INITIAL_STATE = {currentUniverse: getInitialState({}), createUniverse: getInitialState({}), editUniverse: getInitialState({}), deleteUniverse: getInitialState({}),
                       universeList: getInitialState([]), error: null, showModal: false, visibleModal: "",
                       formSubmitSuccess: false, universeConfigTemplate: getInitialState({}), universeResourceTemplate: getInitialState({}),
                       currentPlacementStatus: null, fetchUniverseMetadata: false};

export default function(state = INITIAL_STATE, action) {
  let error;
  switch(action.type) {
    // Universe CRUD Operations
    case CREATE_UNIVERSE:
      return setLoadingState(state, "createUniverse", {});
    case CREATE_UNIVERSE_RESPONSE:
      return setPromiseResponse(state, "createUniverse", action);
    case EDIT_UNIVERSE:
      return setLoadingState(state, "editUniverse", {});
    case EDIT_UNIVERSE_RESPONSE:
      return setPromiseResponse(state, "editUniverse", action);
    case DELETE_UNIVERSE:
      return setLoadingState(state, "deleteUniverse", {});
    case DELETE_UNIVERSE_RESPONSE:
      return setPromiseResponse(state, "deleteUniverse", action);
    // Modal Operations
    case OPEN_DIALOG:
      return { ...state, showModal: true, visibleModal: action.payload};
    case CLOSE_DIALOG:
      return { ...state, showModal: false, visibleModal: "", universeConfigTemplate: getInitialState({}), universeResourceTemplate: getInitialState({})};
    // Universe GET operations
    case FETCH_UNIVERSE_INFO:
      return setLoadingState(state, "currentUniverse", {});
    case FETCH_UNIVERSE_INFO_RESPONSE:
      return setPromiseResponse(state, "currentUniverse", action);
    case RESET_UNIVERSE_INFO:
      return { ...state, currentUniverse: getInitialState({})};
    case FETCH_UNIVERSE_LIST:
      return setLoadingState(state, "universeList", []);
    case FETCH_UNIVERSE_LIST_RESPONSE:
      return {...setPromiseResponse(state, "universeList", action), fetchUniverseMetadata: false};
    case RESET_UNIVERSE_LIST:
      return { ...state, universeList: getInitialState([]), universeCurrentCostList: [], currentTotalCost: 0, error: null};
    // Universe Tasks Operations
    case FETCH_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: _.assign(state.loading, {universeTasks: true})};
    case FETCH_UNIVERSE_TASKS_SUCCESS:
      return { ...state, universeTasks: action.payload.data, error: null, loading: _.assign(state.loading, {universeTasks: false})};
    case FETCH_UNIVERSE_TASKS_FAILURE:
      return { ...state, universeTasks: [], error: error, loading: _.assign(state.loading, {universeTasks: false})};
    case RESET_UNIVERSE_TASKS:
      return { ...state, universeTasks: [], error: null, loading: false};
    // Universe Configure Operations
    case CONFIGURE_UNIVERSE_TEMPLATE:
      return setLoadingState(state, "universeConfigTemplate", {});
    case CONFIGURE_UNIVERSE_TEMPLATE_RESPONSE:
      return setPromiseResponse(state, "universeConfigTemplate", action);
    case CONFIGURE_UNIVERSE_TEMPLATE_SUCCESS:
      return setSuccessState(state, "universeConfigTemplate", action.payload.data);
    case CONFIGURE_UNIVERSE_RESOURCES:
      return setLoadingState(state, "universeResourceTemplate", {});
    case CONFIGURE_UNIVERSE_RESOURCES_RESPONSE:
      return setPromiseResponse(state, "universeResourceTemplate", action);
    // Universe Rolling Upgrade Operations
    case ROLLING_UPGRADE:
      return { ...state, error: null};
    case ROLLING_UPGRADE_SUCCESS:
      return { ...state, error: null, formSubmitSuccess: true};
    case ROLLING_UPGRADE_FAILURE:
      return {...state, error: action.payload.data.error, formSubmitSuccess: false};
    case RESET_ROLLING_UPGRADE:
      return { ...state, error: null};
    // Universe I/O Metrics Operations
    case SET_UNIVERSE_METRICS:
      var currentUniverseList = _.clone(state.universeList);
      var universeReadWriteMetricList = action.payload.data.disk_iops_by_universe.data;
      if (isNonEmptyArray(universeReadWriteMetricList)) {
        universeReadWriteMetricList.forEach(function(metricData, metricIdx) {
          for (var counter = 0; counter < currentUniverseList.data.length; counter++) {
            if (currentUniverseList.data[counter].universeDetails.nodePrefix === metricData.name) {
              currentUniverseList.data[counter][metricData.labels["type"]] = metricData;
            }
          }
        });
      }
      return {...state, universeList: currentUniverseList};
    case SET_PLACEMENT_STATUS:
      return {...state, currentPlacementStatus: action.payload}
    case RESET_UNIVERSE_CONFIGURATION:
      return {...state, currentPlacementStatus: null, universeResourceTemplate: getInitialState({}), universeConfigTemplate: getInitialState({})}
    case FETCH_UNIVERSE_METADATA:
      return {...state, fetchUniverseMetadata: true};
    default:
      return state;
  }
}
