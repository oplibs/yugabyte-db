// Copyright (c) YugaByte, Inc.
import { FETCH_TASK_PROGRESS, FETCH_TASK_PROGRESS_RESPONSE, RESET_TASK_PROGRESS,
  FETCH_CUSTOMER_TASKS, FETCH_CUSTOMER_TASKS_SUCCESS, FETCH_CUSTOMER_TASKS_FAILURE,
  RESET_CUSTOMER_TASKS } from '../actions/tasks';

import { getInitialState, setInitialState, setLoadingState, setPromiseResponse }  from '../utils/PromiseUtils';

const INITIAL_STATE = {
  taskProgressData: getInitialState(),
  currentTaskList: {},
  customerTaskList: []
};

export default function(state = INITIAL_STATE, action) {
  switch(action.type) {
    case FETCH_TASK_PROGRESS:
      return setLoadingState(state, "taskProgressData", {});
    case FETCH_TASK_PROGRESS_RESPONSE:
      return setPromiseResponse(state, "taskProgressData", action);
    case RESET_TASK_PROGRESS:
      return setInitialState(state, "taskProgressData", {});
    case FETCH_CUSTOMER_TASKS:
      return {...state}
    case FETCH_CUSTOMER_TASKS_SUCCESS:
      var taskData = action.payload.data;
      var taskListResultArray = [];
      Object.keys(taskData).forEach(function(taskIdx){
        taskData[taskIdx].forEach(function(taskItem){
          taskItem.universeUUID = taskIdx;
          taskListResultArray.push(taskItem);
        })
      });
      return {...state, customerTaskList: taskListResultArray.sort((a, b) => b.createTime - a.createTime)}
    case FETCH_CUSTOMER_TASKS_FAILURE:
      return {...state, customerTaskList: action.payload.error}
    case RESET_CUSTOMER_TASKS:
      return {...state, customerTaskList: []};
    default:
      return state;
  }
}
