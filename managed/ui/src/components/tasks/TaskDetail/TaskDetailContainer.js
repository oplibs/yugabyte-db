// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { TaskDetail } from '../../tasks';
import { fetchCustomerTasks, fetchCustomerTasksSuccess, fetchCustomerTasksFailure,
          resetCustomerTasks, fetchFailedSubTasks, fetchFailedSubTasksResponse, fetchTaskProgress, fetchTaskProgressResponse} from '../../../actions/tasks';

const mapDispatchToProps = (dispatch) => {
  return {
    fetchFailedTaskDetail: (taskUUID) => {
      dispatch(fetchFailedSubTasks(taskUUID))
        .then((response)=> {
          dispatch(fetchFailedSubTasksResponse(response));
        });
    },
    fetchTaskList: () => {
      dispatch(fetchCustomerTasks())
        .then((response) => {
          if (!response.error) {
            dispatch(fetchCustomerTasksSuccess(response.payload));
          } else {
            dispatch(fetchCustomerTasksFailure(response.payload));
          }
        });
    },
    fetchCurrentTaskDetail: (taskUUID) => {
      dispatch(fetchTaskProgress(taskUUID))
        .then((response) => {
          dispatch(fetchTaskProgressResponse(response.payload));
        });
    },
    resetCustomerTasks: () => {
      dispatch(resetCustomerTasks());
    }
  };
};

function mapStateToProps(state, ownProps) {
  return {
    universe: state.universe,
    tasks: state.tasks
  };
}

export default connect(mapStateToProps, mapDispatchToProps)(TaskDetail);
