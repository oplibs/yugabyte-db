// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { UniverseConsoleContainer } from '../components/universes';

export default class UniverseConsole extends Component {
  render() {
    return (
      <div className="dashboard-container">
        <UniverseConsoleContainer />
      </div>
    );
  }
}
