// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
var Plotly = require('plotly.js/lib/core');
import { removeNullProperties, isValidObject } from '../../utils/ObjectUtils';

export default class MetricsPanel extends Component {

  static propTypes = {
    metric: PropTypes.object.isRequired,
    metricKey: PropTypes.string.isRequired
  }
  componentDidMount() {
    const { metricKey, metric } = this.props;
    if (isValidObject(metric)) {
      // Remove Null Properties from the layout
      removeNullProperties(metric.layout);

      // TODO: send this data from backend.
      metric.layout["autosize"] = false;
      metric.layout["width"] = 650;
      metric.layout["height"] = 500;

      Plotly.newPlot(metricKey, metric.data, metric.layout, {displayModeBar: false});
    }
  }

  render() {
    // TODO: handle empty metric data case.
    return (
      <div id={this.props.metricKey} />
    );
  }
}
