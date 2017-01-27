// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';

import { UniverseDetailContainer } from '../components/universes';
import Universes from './Universes';

class UniverseDetail extends Component {
  render() {
    return (
      <Universes>
        <UniverseDetailContainer uuid={this.props.params.uuid}/>
      </Universes>
    );
  }
}
export default UniverseDetail;
