import React from 'react';
import { Col, Row } from 'react-bootstrap';
import { Link } from 'react-router';
import { IReplication } from '../IClusterReplication';

export function ReplicationOverview({
  replication,
  destinationUniverse
}: {
  replication: IReplication;
  destinationUniverse: any;
}) {
  const {
    universeDetails: { nodeDetailsSet }
  } = destinationUniverse;

  return (
    <>
      <Row className="replication-overview">
        <Col lg={12}>
          <Row>
            <Col lg={2} className="noLeftPadding replication-label">
              Replication started
            </Col>
            <Col lg={2}>{replication.createTime}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={2} className="noLeftPadding replication-label">
              Replication last modified
            </Col>
            <Col lg={2}>{replication.modifyTime}</Col>
          </Row>
        </Col>
      </Row>
      <div className="replication-divider" />
      <Row style={{ paddingLeft: '20px' }}>
        <Col lg={12}>
          <b>Replication's Target Universe</b>
        </Col>
      </Row>
      <div className="replication-divider" />
      <Row className="replication-target-universe">
        <Col lg={12}>
          <Row>
            <Col lg={3} className="replication-label">
              Name
            </Col>
            <Col lg={3}>
              <Link to={`/universes/${destinationUniverse.universeUUID}`}>
                {destinationUniverse.name}
              </Link>
            </Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={3} className="replication-label">
              UUID
            </Col>
            <Col lg={3}>{replication.targetUniverseUUID}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={3} className="replication-label">
              Master node address
            </Col>
            <Col lg={3}>{replication.masterAddress}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={3} className="replication-label">
              Provider
            </Col>
            <Col lg={3}>{nodeDetailsSet[0].cloudInfo.cloud}</Col>
          </Row>
          <div className="replication-divider" />
          <Row>
            <Col lg={3} className="replication-label">
              Region
            </Col>
            <Col lg={3}>{nodeDetailsSet[0].cloudInfo.region}</Col>
          </Row>
          <div className="replication-divider" />
        </Col>
      </Row>
    </>
  );
}
