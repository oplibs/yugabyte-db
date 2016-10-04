// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import {Button} from 'react-bootstrap';
export default class YBButton extends Component {

  render() {
    const {btnClass, btnText, btnIcon, btnSize, btnType} = this.props;
    return (
      <Button bsClass={btnClass} type={btnType}
              onClick={this.props.onClick} bsSize={btnSize}>
        <i className={btnIcon}></i>
        {btnText}
      </Button>
    )
  }
}
