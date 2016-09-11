// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import TaskProgressWidget from './TaskProgressWidget';
import TaskProgressBar from './TaskProgressBar';

export default class TaskProgress extends Component {
  static contextTypes = {
    router: PropTypes.object
  }

  static propTypes = {
    taskUUIDs: PropTypes.array,
    type: PropTypes.oneOf(['Bar', 'Widget'])
  }

  static defaultProps = {
    type: 'Widget'
  }

  componentDidMount() {
    const { taskUUIDs } = this.props;
    if (taskUUIDs && taskUUIDs.length > 0) {
      // TODO, currently we only show one of the tasks, we need to
      // implement a way to show all the tasks against a universe
      this.props.fetchTaskProgress(taskUUIDs[0]);
    }
  }

  componentWillUnmount() {
    this.props.resetTaskProgress();
  }

  render() {
    const { taskUUIDs, tasks: { taskProgressData, loading}, type } = this.props;
    if (taskUUIDs.length === 0) {
      return <span />;
    } else if (loading || taskProgressData.length === 0) {
      return <div className="container">Loading...</div>;
    } else if (taskProgressData.status === "Success" ||
               taskProgressData.status === "Failure") {
      // TODO: Better handle/display the success/failure case
      return <span />;
    }

    if ( type === "Widget" ) {
      return <TaskProgressWidget progressData={taskProgressData} />;
    } else {
      return <TaskProgressBar progressData={taskProgressData} />;
    }
  }
}
