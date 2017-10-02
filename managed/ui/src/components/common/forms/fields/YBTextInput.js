// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import { FormControl } from 'react-bootstrap';
import { isFunction } from 'lodash';
import { YBLabel } from 'components/common/descriptors';
import { isDefinedNotNull } from "../../../../utils/ObjectUtils";

// TODO: Make default export after checking all corresponding imports.
export class YBTextInput extends Component {
  static defaultProps = {
    isReadOnly: false
  };

  render() {
    const self = this;
    const { input, type, className, placeHolder, onValueChanged, isReadOnly, normalizeOnBlur, initValue } = this.props;

    function onChange(event) {
      if (isFunction(onValueChanged)) {
        onValueChanged(event.target.value);
      }
      self.props.input.onChange(event.target.value);
    }

    function onBlur(event) {
      if (isFunction(normalizeOnBlur)) {
        self.props.input.onBlur(normalizeOnBlur(event.target.value));
      } else {
        self.props.input.onBlur(event.target.value);
      }
    }

    if (isDefinedNotNull(initValue)) {
      input["value"] = initValue;
    }

    return (
      <FormControl {...input} placeholder={placeHolder} type={type} className={className} onChange={onChange}
                   readOnly={isReadOnly} onBlur={onBlur} />
    );
  }
}

export default class YBTextInputWithLabel extends Component {
  render() {
    const { label, meta, insetError, infoContent, infoTitle, infoPlacement, ...otherProps } = this.props;
    return (
      <YBLabel label={label} meta={meta} insetError={insetError} infoContent={infoContent} infoTitle={infoTitle}
               infoPlacement={infoPlacement}>
        <YBTextInput {...otherProps} />
      </YBLabel>
    );
  }
}

export class YBControlledTextInput extends Component {
  render() {
    const { label, meta, input, type, className, placeHolder, onValueChanged, isReadOnly, val, infoContent, infoTitle,
      infoPlacement } = this.props;
    return (
      <YBLabel label={label} meta={meta} infoContent={infoContent} infoTitle={infoTitle} infoPlacement={infoPlacement}>
        <FormControl {...input} placeholder={placeHolder} type={type} className={className}
                     onChange={onValueChanged} readOnly={isReadOnly} value={val}/>
      </YBLabel>
    );
  }
}

// TODO: Deprecated. Rename all YBInputField references to YBTextInputWithLabel.
export const YBInputField = YBTextInputWithLabel;
