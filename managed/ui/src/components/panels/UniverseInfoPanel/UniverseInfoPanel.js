// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import PropTypes from 'prop-types';
import { FormattedDate } from 'react-intl';
import { ROOT_URL } from '../../../config';
import { DescriptionList } from '../../common/descriptors';

export default class UniverseInfoPanel extends Component {
  static propTypes = {
    universeInfo: PropTypes.object.isRequired
  };

  render() {
    const { universeInfo, customerId } = this.props;
    const { universeDetails } = universeInfo;
    const { userIntent } = universeDetails;
    var universeId = universeInfo.universeUUID;
    var formattedCreationDate =
      <FormattedDate value={universeInfo.creationDate}
                     year='numeric' month='long' day='2-digit'
                     hour='2-digit' minute='2-digit' second='2-digit' timeZoneName='short' />;
    const endpointUrl = ROOT_URL + "/customers/" + customerId +
                        "/universes/" + universeId + "/yqlservers";
    const endpoint =
      <a href={endpointUrl} target="_blank">Endpoint &nbsp;
        <i className="fa fa-external-link" aria-hidden="true"></i>
      </a>;
    var universeInfoItems = [
      {name: "Universe ID", data: universeId},
      {name: "Customer ID", data: customerId},
      {name: "Launch Time", data: formattedCreationDate},
      {name: "YQL Proxies", data: endpoint},
      {name: "Server Version", data: userIntent.ybSoftwareVersion || 'n/a'},
    ];

    return (
      <DescriptionList listItems={universeInfoItems} />
    );
  }
}
