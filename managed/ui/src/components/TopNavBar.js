// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import '../stylesheets/TopNavBar.css';
import 'react-fa';
import { MenuItem , NavDropdown } from 'react-bootstrap';
import TaskAlertsContainer from '../containers/TaskAlertsContainer';

export default class TopNavBar extends Component {
	constructor(props) {
    super(props);
    this.handleLogout = this.handleLogout.bind(this);
  }

  handleLogout(event) {
    this.props.logoutProfile();
  }

	render() {
		return (
			<div>
			  <ul className="nav navbar-top-links navbar-right">
				  <NavDropdown eventKey="2" title={<i className="fa fa-user fa-fw"></i>} id="profile-dropdown">
					  <MenuItem eventKey="2.1" href="/profile">
						  <i className="fa fa-user fa-fw"></i>Profile
					  </MenuItem>
					  <MenuItem divider />
					  <MenuItem eventKey="2.2" href="/login" id="logoutLink" onClick={this.handleLogout}>
						  <i className="fa fa-sign-out fa-fw"></i>Logout
					  </MenuItem>
				  </NavDropdown>
			  </ul>
			  <ul className="nav navbar-top-links navbar-right">
					<NavDropdown eventKey="1" title={<i className="fa fa-bars"></i>} id="task-alert-dropdown">
						<TaskAlertsContainer eventKey="1"/>
					</NavDropdown>
				</ul>
			</div>
		);
	}
}
