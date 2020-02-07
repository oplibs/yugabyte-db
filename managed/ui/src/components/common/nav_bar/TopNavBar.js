// Copyright (c) YugaByte, Inc.

import Cookies from 'js-cookie';
import React, { Component } from 'react';
import 'react-fa';
import { MenuItem , NavDropdown, Navbar, Nav } from 'react-bootstrap';
import { Link } from 'react-router';
import YBLogo from '../YBLogo/YBLogo';
import './stylesheets/TopNavBar.scss';
import { getPromiseState } from 'utils/PromiseUtils';
import { LinkContainer } from 'react-router-bootstrap';
import { isNotHidden, isDisabled } from 'utils/LayoutUtils';

class YBMenuItem extends Component {
  render() {
    const { disabled, to, id, className, onClick } = this.props;
    if (disabled) return (
      <li>
        <div className={className}>
          {this.props.children}
        </div>
      </li>
    );

    return (
      <LinkContainer to={to} id={id}>
        <MenuItem className={className} onClick={onClick}>
          {this.props.children}
        </MenuItem>
      </LinkContainer>
    );
  }
}

export default class TopNavBar extends Component {
  handleLogout = event => {
    // Don't remove all localStorage items
    localStorage.removeItem('authToken');
    localStorage.removeItem('apiToken');
    Cookies.remove('authToken');
    Cookies.remove('apiToken');
    Cookies.remove('customerId');
    this.props.logoutProfile();
  };

  render() {
    const { customer: { currentCustomer }} = this.props;
    const customerEmail = getPromiseState(currentCustomer).isSuccess()
        ? currentCustomer.data.email
        : "";

    // TODO(bogdan): icon for logs...
    return (
      <Navbar fixedTop>
        {getPromiseState(currentCustomer).isSuccess() && isNotHidden(currentCustomer.data.features, "menu.sidebar") &&
          <Navbar.Header>
            <Link to="/" className="left_col text-center">
              <YBLogo />
            </Link>
          </Navbar.Header>
        }
        <div className="flex-grow"></div>
        {getPromiseState(currentCustomer).isSuccess() && isNotHidden(currentCustomer.data.features, "main.dropdown") &&
        <Nav pullRight>
          <NavDropdown  eventKey="2" title={<span><i className="fa fa-user fa-fw"></i> {customerEmail}</span>} id="profile-dropdown">
            {isNotHidden(currentCustomer.data.features, "main.profile") &&
              <YBMenuItem to={"/profile"} disabled={isDisabled(currentCustomer.data.features, "main.profile")}>
                <i className="fa fa-user fa-fw"></i>Profile
              </YBMenuItem>
            }
            {isNotHidden(currentCustomer.data.features, "main.logs") &&
              <YBMenuItem to={"/logs"} disabled={isDisabled(currentCustomer.data.features, "main.logs")}>
                <i className="fa fa-file fa-fw"></i>Logs
              </YBMenuItem>
            }
            {isNotHidden(currentCustomer.data.features, "main.schedules") &&
              <YBMenuItem to={"/schedules"} disabled={isDisabled(currentCustomer.data.features, "main.schedules")}>
                <i className="fa fa-calendar-o fa-fw"></i>Schedules
              </YBMenuItem>
            }
            {isNotHidden(currentCustomer.data.features, "main.certificates") &&
              <YBMenuItem to={"/certificates"} disabled={isDisabled(currentCustomer.data.features, "main.certificates")}>
                <i className="fa fa-lock fa-fw"></i>Certificates
              </YBMenuItem>
            }
            {isNotHidden(currentCustomer.data.features, "main.releases") &&
              <YBMenuItem to={"/releases"} disabled={isDisabled(currentCustomer.data.features, "main.releases")}>
                <i className="fa fa-code-fork fa-fw"></i>Releases
              </YBMenuItem>
            }
            <YBMenuItem to="/login" id="logoutLink" onClick={this.handleLogout}>
              <i className="fa fa-sign-out fa-fw"></i>Logout
            </YBMenuItem>
          </NavDropdown>
        </Nav>}
      </Navbar>
    );
  }
}
