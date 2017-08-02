// Copyright (c) YugaByte, Inc.

import { connect } from 'react-redux';
import { reduxForm } from 'redux-form';
import {OnPremProviderAndAccessKey} from '../../../config';
import {setOnPremConfigData} from '../../../../actions/cloud';
import {isDefinedNotNull, isNonEmptyObject} from 'utils/ObjectUtils';

const mapDispatchToProps = (dispatch, ownProps) => {
  return {
    setOnPremProviderAndAccessKey: (formData) => {
      if (!ownProps.isEditProvider) {
        var formSubmitVals = {
          provider: {name: formData.name},
          key: {code: formData.name.toLowerCase().replace(/ /g, "-") + "-key",
                privateKeyContent: formData.privateKeyContent,
                sshUser: formData.sshUser }
        };
        dispatch(setOnPremConfigData(formSubmitVals));
      }
      ownProps.nextPage();
    }
  }
}

const mapStateToProps = (state, ownProps) => {
  let initialFormValues = {};
  const {cloud: {onPremJsonFormData}} = state;
  if (ownProps.isEditProvider && isNonEmptyObject(onPremJsonFormData)) {
    initialFormValues = {
      name: onPremJsonFormData.provider.name,
      keyCode: onPremJsonFormData.key.code,
      privateKeyContent: onPremJsonFormData.key.privateKeyContent,
      sshUser: onPremJsonFormData.key.sshUser,
      machineTypeList : onPremJsonFormData.instanceTypes.map(function (item) {
        return {
          code: item.instanceTypeCode,
          numCores: item.numCores,
          memSizeGB: item.memSizeGB,
          volumeSizeGB: item.volumeDetailsList[0].volumeSizeGB,
          mountPath: item.volumeDetailsList.map(function(volItem) {
            return volItem.mountPath
          }).join(", ")
        }
      }),
      regionsZonesList: onPremJsonFormData.regions.map(function(regionZoneItem, rIdx){
        return {code: regionZoneItem.code,
                location: Number(regionZoneItem.latitude) + ", " + Number(regionZoneItem.longitude),
                zones: regionZoneItem.zones.map(function(zoneItem){
                  return zoneItem
                }).join(", ")}
        })
    };
  }
  return {
    onPremJsonFormData: state.cloud.onPremJsonFormData,
    cloud: state.cloud,
    initialValues: initialFormValues
  };
}

const validate = values => {
  const errors = {};
  if (!isDefinedNotNull(values.name)) {
    errors.name = 'Required';
  }
  if (!isDefinedNotNull(values.sshUser)) {
    errors.sshUser = 'Required';
  }
  if (!isDefinedNotNull(values.privateKeyContent)) {
    errors.privateKeyContent = 'Required';
  }
  return errors;
};

var onPremProviderConfigForm = reduxForm({
  form: 'onPremConfigForm',
  validate,
  destroyOnUnmount: false,
  enableReinitialize: true,
  keepDirtyOnReinitialize: true
});

export default connect(mapStateToProps, mapDispatchToProps)(onPremProviderConfigForm(OnPremProviderAndAccessKey));
