// Copyright (c) YugaByte, Inc.

import React, { Component, PropTypes } from 'react';
import { PageHeader } from 'react-bootstrap';
import { Field } from 'redux-form';
import { YBButton, YBInputField } from '../fields';
import YBLogo from '../../YBLogo/YBLogo';
import {browserHistory} from 'react-router';

class RegisterForm extends Component {
  constructor(props) {
    super(props);
    this.submitRegister = this.submitRegister.bind(this);
  }

  componentWillReceiveProps(nextProps) {
    if(nextProps.customer.status === 'authenticated' && nextProps.customer.customer && !nextProps.customer.error) {
      browserHistory.push('/');
    }
  }

  submitRegister(formValues) {
    const {registerCustomer} = this.props;
    registerCustomer(formValues);
  }

  render() {
    const { handleSubmit, submitting } = this.props;
    return (
      <div className="container full-height dark-background flex-vertical-middle">
        <div className="col-sm-6 dark-form">
          <PageHeader bsClass="dark-form-heading">
            <YBLogo />
            <span>Admin Console Registration</span>
          </PageHeader>
          <form onSubmit={handleSubmit(this.props.registerCustomer.bind(this))}>
            <div className={`alert alert-danger form-error-alert
              ${this.props.customer.error ? '': 'hide'}`}>
                {<strong>{this.props.customer.error}</strong>}
            </div>
            <div className="form-right-aligned-labels">
              <Field name="name" type="text" component={YBInputField} label="Full Name"/>
              <Field name="email" type="email" component={YBInputField} label="Email"/>
              <Field name="password" type="password" component={YBInputField} label="Password"/>
              <Field name="confirmPassword" type="password" component={YBInputField} label="Confirm Password"/>
            </div>
            <div className="clearfix">
              <YBButton btnType="submit" btnDisabled={submitting} btnClass="btn btn-default bg-orange pull-right" btnText="Register"/>
            </div>
          </form>
        </div>
      </div>
    );
  }
}

export default RegisterForm;
