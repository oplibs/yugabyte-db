// Copyright (c) YugaByte, Inc.

import React, { Component } from 'react';
import _ from 'lodash';
import { isValidObject, isDefinedNotNull, isNonEmptyArray } from 'utils/ObjectUtils';
import { OnPremConfigWizardContainer, OnPremConfigJSONContainer, OnPremSuccessContainer } from '../../config';
import { YBButton } from '../../common/forms/fields';
import emptyDataCenterConfig from '../templates/EmptyDataCenterConfig.json';
import './OnPremConfiguration.scss';
import { getPromiseState } from 'utils/PromiseUtils';
import { YBLoadingIcon } from '../../common/indicators';

const PROVIDER_TYPE = "onprem";
const initialState = {
  isEditProvider: false,
  isJsonEntry: false,
  isAdditionalHostOptionsOpen: false,
  configJsonVal: JSON.stringify(JSON.parse(JSON.stringify(emptyDataCenterConfig)), null, 2),
  bootstrapSteps: [
    {type: "provider", name: "Create Provider", status: "Initializing"},
    {type: "instanceType", name: "Create Instance Types", status: "Initializing"},
    {type: "region", name: "Create Regions", status: "Initializing"},
    {type: "zone", name: "Create Zones", status: "Initializing"},
    {type: "node", name: "Create Node Instances", status: "Initializing"},
    {type: "accessKey", name: "Create Access Keys", status: "Initializing"}
  ],
  regionsMap: {},
  zonesMap: {},
  providerUUID: null,
  numZones: 0,
  numNodesConfigured: 0,
  numRegions: 0,
  numInstanceTypes: 0,
  numInstanceTypesConfigured: 0
};

export default class OnPremConfiguration extends Component {

  constructor(props) {
    super(props);
    this.state = _.clone(initialState, true);
    this.toggleJsonEntry = this.toggleJsonEntry.bind(this);
    this.toggleAdditionalOptionsModal = this.toggleAdditionalOptionsModal.bind(this);
    this.submitJson = this.submitJson.bind(this);
    this.updateConfigJsonVal = this.updateConfigJsonVal.bind(this);
    this.submitWizardJson = this.submitWizardJson.bind(this);
    this.serializeStringToJson = this.serializeStringToJson.bind(this);
    this.showEditProviderForm = this.showEditProviderForm.bind(this);
    this.submitEditProvider = this.submitEditProvider.bind(this);
    this.resetEdit = this.resetEdit.bind(this);
  }

  componentWillMount() {
    this.props.resetConfigForm();
  }

  serializeStringToJson(payloadString) {
    if (!_.isString(payloadString)) {
      return payloadString;
    }
    let jsonPayload = {};
    try {
      jsonPayload = JSON.parse(payloadString);
    } catch (e) {
      // Handle case where private key contains newline characters and hence is not valid json
      let jsonPayloadTokens = payloadString.split("\"privateKeyContent\":");
      let privateKeyBlob = jsonPayloadTokens[1].split("}")[0].trim();
      let privateKeyString = privateKeyBlob.replace(/\n/g, "\\n");
      let newJsonString = jsonPayloadTokens[0] + "\"privateKeyContent\": " + privateKeyString + "\n}\n}";
      jsonPayload = JSON.parse(newJsonString);
    }
    // Add sshUser to Node Payload, if not added
    jsonPayload.nodes.map( (node) => node.sshUser = node.sshUser || jsonPayload.key.sshUser )
    return jsonPayload;
  }

  showEditProviderForm() {
    this.setState({isEditProvider: true});
  }

  componentWillReceiveProps(nextProps) {
    const { cloudBootstrap: {data: { response, type }, error, promiseState}} = nextProps;
    let bootstrapSteps = this.state.bootstrapSteps;
    let currentStepIndex = bootstrapSteps.findIndex( (step) => step.type === type );
    if (currentStepIndex !== -1) {
      if (promiseState.isLoading()) {
        bootstrapSteps[currentStepIndex].status = "Running";
      } else {
        bootstrapSteps[currentStepIndex].status = error ? "Error" : "Success";
      }
      this.setState({bootstrapSteps: bootstrapSteps});
    }
    if (isValidObject(response)) {

      let payloadString = _.clone(this.state.configJsonVal);
      const config = this.serializeStringToJson(payloadString);
      const isEdit = this.state.isEditProvider;
      const numZones = config.regions.reduce((total, region) => {
        return total + region.zones.length
      }, 0);
      switch (type) {
        case "provider":
          // Launch configuration of instance types
          this.setState({
            regionsMap: {},
            zonesMap: {},
            providerUUID: response.uuid,
            numRegions: config.regions.length,
            numInstanceTypes: config.instanceTypes.length,
            numZones: numZones,
            numNodesConfigured: 0,
            numInstanceTypesConfigured: 0
          });
          bootstrapSteps[currentStepIndex + 1].status = "Running";
          this.props.createOnPremInstanceTypes(PROVIDER_TYPE, response.uuid, config, isEdit);
          break;
        case "instanceType":
          // Launch configuration of regions
          let numInstanceTypesConfigured = this.state.numInstanceTypesConfigured;
          numInstanceTypesConfigured++;
          this.setState({numInstanceTypesConfigured: numInstanceTypesConfigured});
          if (numInstanceTypesConfigured === this.state.numInstanceTypes) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            if (this.state.isEditProvider && this.state.numRegions === 0) {
              this.resetEdit();
            } else {
              this.props.createOnPremRegions(this.state.providerUUID, config, isEdit);
            }
          }
          break;
        case "region":
          // Update regionsMap until done
          let regionsMap = this.state.regionsMap;
          regionsMap[response.code] = response.uuid;
          this.setState({regionsMap: regionsMap});
          // Launch configuration of zones once all regions are bootstrapped
          if (Object.keys(regionsMap).length === this.state.numRegions) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            this.props.createOnPremZones(this.state.providerUUID, regionsMap, config, isEdit);
          }
          break;
        case "zone":
          // Update zonesMap until done
          let zonesMap = this.state.zonesMap;
          zonesMap[response.code] = response.uuid;
          this.setState({zonesMap: zonesMap});
          // Launch configuration of node instances once all availability zones are bootstrapped
          if (Object.keys(zonesMap).length === this.state.numZones) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            // If Edit Case, then jump to success
            if (this.state.isEditProvider) {
              this.resetEdit();
            } else if (isNonEmptyArray(config.nodes)) {
                this.props.createOnPremNodes(zonesMap, config);
            } else {
                this.props.createOnPremAccessKeys(this.state.providerUUID, this.state.regionsMap, config);
            }
          }
          break;
        case "node":
          // Update numNodesConfigured until done
          let numNodesConfigured = this.state.numNodesConfigured;
          numNodesConfigured++;
          this.setState({numNodesConfigured: numNodesConfigured});
          // Launch configuration of access keys once all node instances are bootstrapped
          if (numNodesConfigured === config.nodes.length) {
            bootstrapSteps[currentStepIndex + 1].status = "Running";
            if (config.key && _.isString(config.key.privateKeyContent)) {
              this.props.createOnPremAccessKeys(this.state.providerUUID, this.state.regionsMap, config);
            }
          }
          break;
        case "accessKey":
          this.setState(_.clone(initialState, true));
          this.props.onPremConfigSuccess();
          break;
        default:
          break;
      }
    }
  }

  resetEdit() {
    this.setState(_.clone(initialState, true));
    this.props.resetConfigForm();
    this.props.onPremConfigSuccess();
  }

  toggleJsonEntry() {
    this.setState({'isJsonEntry': !this.state.isJsonEntry})
  }

  toggleAdditionalOptionsModal() {
    this.setState({isAdditionalHostOptionsOpen: !this.state.isAdditionalHostOptionsOpen});
  }

  updateConfigJsonVal(newConfigJsonVal) {
    this.setState({configJsonVal: newConfigJsonVal});
  }

  submitJson() {
    if (this.state.isJsonEntry) {
      this.props.createOnPremProvider(PROVIDER_TYPE, this.serializeStringToJson(this.state.configJsonVal));
    }
  }

  submitEditProvider(payloadData) {
    const {cloud: {providers}} = this.props;
    let self = this;
    let currentProvider = providers.data.find((provider)=>(provider.code === "onprem"));
    let totalNumRegions  = 0;
    let totalNumInstances = 0;
    let totalNumZones = 0;
    payloadData.regions.forEach(function(region){
      if (region.isBeingEdited) {
        totalNumRegions ++;
        totalNumZones += region.zones.length;
      }
    })
    payloadData.instanceTypes.forEach(function(instanceType){
      if (instanceType.isBeingEdited) {
        totalNumInstances ++;
      }
    });
    if (totalNumInstances === 0 && totalNumRegions === 0) {
      this.setState({configJsonVal: payloadData, isEditProvider: false});
    } else {
      this.setState({numRegions: totalNumRegions,
                    numZones: totalNumZones,
                    numInstanceTypes: totalNumInstances,
                    configJsonVal: payloadData,
                    providerUUID: currentProvider.uuid
      }, function () {
        if (totalNumInstances > 0) {
          self.props.createOnPremInstanceTypes(currentProvider.code, currentProvider.uuid, payloadData, true);
        } else {
          // If configuring only regions, then directly jump to region configure
          self.props.createOnPremRegions(currentProvider.uuid, payloadData, true);
        }
      });
    }
  }

  submitWizardJson(payloadData) {
    this.setState({configJsonVal: payloadData})
    this.props.createOnPremProvider(PROVIDER_TYPE, payloadData);
  }

  render() {
    const { configuredProviders } = this.props;
    if (getPromiseState(configuredProviders).isInit() || getPromiseState(configuredProviders).isError()) {
      return <span/>;
    }
    else if (getPromiseState(configuredProviders).isLoading()) {
      return <YBLoadingIcon/>;
    } else if (getPromiseState(configuredProviders).isSuccess()) {
      var providerFound = configuredProviders.data.find(provider => provider.code === 'onprem');
      if (isDefinedNotNull(providerFound)) {
        if (this.state.isEditProvider) {
          return <OnPremConfigWizardContainer submitWizardJson={this.submitWizardJson} isEditProvider={this.state.isEditProvider} submitEditProvider={this.submitEditProvider}/>;
        }
        return <OnPremSuccessContainer showEditProviderForm={this.showEditProviderForm}/>;
      }
    }
    var switchToJsonEntry = <YBButton btnText={"Switch to JSON View"} btnClass={"btn btn-default pull-left"} onClick={this.toggleJsonEntry}/>;
    var switchToWizardEntry = <YBButton btnText={"Switch to Wizard View"} btnClass={"btn btn-default pull-left"} onClick={this.toggleJsonEntry}/>;
    let ConfigurationDataForm = <OnPremConfigWizardContainer switchToJsonEntry={switchToJsonEntry} submitWizardJson={this.submitWizardJson}/>;
    if (this.state.isJsonEntry) {
      ConfigurationDataForm = <OnPremConfigJSONContainer updateConfigJsonVal={this.updateConfigJsonVal}
                                                         configJsonVal={_.isString(this.state.configJsonVal) ? this.state.configJsonVal : JSON.stringify(JSON.parse(JSON.stringify(this.state.configJsonVal)), null, 2)}
                                                         switchToWizardEntry={switchToWizardEntry} submitJson={this.submitJson}/>
    }
    return (
      <div className="on-prem-provider-container">
        {ConfigurationDataForm}
      </div>
    )
  }
}
