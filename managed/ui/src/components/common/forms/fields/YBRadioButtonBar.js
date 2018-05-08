// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { isObject } from 'lodash';
import { isNonEmptyArray } from 'utils/ObjectUtils';
import YBRadioButton from './YBRadioButton';
import { YBLabel } from '../../descriptors';
import _ from 'lodash';

export default class YBRadioButtonBar extends Component {
  constructor(props) {
    super(props);
    this.state = {fieldValue: 0};
  }
  componentWillMount() {
    this.setState({fieldValue: this.props.initialValue});
  }

  radioButtonChecked = event => {
    const {onSelect, isReadOnly} = this.props;
    if (!isReadOnly && onSelect) {
      this.setState({fieldValue: event.target.value});
      onSelect(event.target.value);
    }
  };

  render() {
    const { input, options } = this.props;
    const self = this;
    function radioButtonForOption(option) {
      let value, display;
      if (isNonEmptyArray(option)) {
        [value, display] = option;
      } else if (isObject(option)) {
        value = option.value;
        display = option.display;
      } else {
        value = display = option;
      }
      const isChecked = _.isEqual(self.state.fieldValue.toString(), value.toString());
      function getLabelClass() {
        return 'btn' + (isChecked ? ' btn-orange' : ' btn-default');
      }

      return (
        <YBRadioButton key={value} {...input} fieldValue={value} checkState={isChecked}
          label={display} labelClass={getLabelClass()} onClick={self.radioButtonChecked}>
        </YBRadioButton>
      );
    }
    return (
      <div className="btn-group btn-group-radio" data-toggle="buttons">
        {options.map(radioButtonForOption)}
      </div>
    );
  }
}

export class YBRadioButtonBarDefault extends Component {
  constructor(props) {
    super(props);
    this.state = {fieldValue: 0};
  }
  componentWillMount() {
    this.setState({fieldValue: this.props.initialValue});
  }

  radioButtonChecked = event => {
    const {onSelect, isReadOnly} = this.props;
    if (!isReadOnly && onSelect) {
      this.setState({fieldValue: event.target.value});
      onSelect(event.target.value);
    }
  };

  render() {
    const { input, options } = this.props;
    const self = this;
    function radioButtonForOption(option) {
      let value, display;
      if (isNonEmptyArray(option)) {
        [value, display] = option;
      } else if (isObject(option)) {
        value = option.value;
        display = option.display;
      } else {
        value = display = option;
      }
      const isChecked = _.isEqual(self.state.fieldValue.toString(), value.toString());

      return (
        <YBRadioButton key={value} {...input} fieldValue={value} checkState={isChecked}
          label={display} onClick={self.radioButtonChecked}>
        </YBRadioButton>
      );
    }
    return (
      <div className="btn-group btn-group-radio" data-toggle="buttons">
        {options.map(radioButtonForOption)}
      </div>
    );
  }
}

export class YBRadioButtonBarWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBRadioButtonBar {...otherProps} />
      </YBLabel>
    );
  }
}

export class YBRadioButtonBarDefaultWithLabel extends Component {
  render() {
    const { label, meta, ...otherProps} = this.props;
    return (
      <YBLabel label={label} meta={meta}>
        <YBRadioButtonBarDefault {...otherProps} />
      </YBLabel>
    );
  }
}
