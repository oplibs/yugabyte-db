// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

export default class AuthenticatedComponent extends Component {
  componentWillMount() {
    this.props.fetchSoftwareVersions();
    this.props.fetchUniverseList();
    this.props.getProviderListItems();
    this.props.getSupportedRegionList();
    this.props.fetchHostInfo();
  }

  componentWillUnmount() {
    this.props.resetUniverseList();
  }

  render() {
    return (
      <div>
        {this.props.children}
      </div>
    )
  }
}
