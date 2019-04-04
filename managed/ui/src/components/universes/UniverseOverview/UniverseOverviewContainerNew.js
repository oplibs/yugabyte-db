// Copyright YugaByte Inc.

import { UniverseOverviewNew } from '../../universes';
import { connect } from 'react-redux';

function mapStateToProps(state) {
  return {
    currentCustomer: state.customer.currentCustomer,
    tasks: state.tasks,
    tables: state.tables
  };
}

export default connect(mapStateToProps)(UniverseOverviewNew);
