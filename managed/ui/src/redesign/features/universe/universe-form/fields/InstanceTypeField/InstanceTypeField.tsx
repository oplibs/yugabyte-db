import React, { ChangeEvent, FC } from 'react';
import pluralize from 'pluralize';
import { Box } from '@material-ui/core';
import { Controller, useFormContext, useWatch } from 'react-hook-form';
import { useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { api, QUERY_KEY } from '../../utils/api';
import {
  CloudType,
  InstanceType,
  InstanceTypeWithGroup,
  StorageType,
  UniverseFormData
} from '../../utils/dto';
import { YBLabel, YBAutoComplete, YBHelper, YBHelperVariants } from '../../../../../components';
import {
  sortAndGroup,
  DEFAULT_INSTANCE_TYPES,
  isEphemeralAwsStorageInstance
} from './InstanceTypeFieldHelper';
import { INSTANCE_TYPE_FIELD, PROVIDER_FIELD, DEVICE_INFO_FIELD } from '../../utils/constants';

const getOptionLabel = (op: Record<string, string>): string => {
  if (op) {
    const option = (op as unknown) as InstanceType;
    let result = option.instanceTypeCode;
    if (option.numCores && option.memSizeGB) {
      const cores = pluralize('core', option.numCores, true);
      result = `${option.instanceTypeCode} (${cores}, ${option.memSizeGB}GB RAM)`;
    }
    return result;
  }
  return '';
};

const renderOption = (option: Record<string, string>) => {
  return <>{getOptionLabel(option)}</>;
};

export const InstanceTypeField: FC = () => {
  const { control, setValue, getValues } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  const provider = useWatch({ name: PROVIDER_FIELD });
  const deviceInfo = useWatch({ name: DEVICE_INFO_FIELD });

  const handleChange = (e: ChangeEvent<{}>, option: any) => {
    setValue(INSTANCE_TYPE_FIELD, option?.instanceTypeCode, { shouldValidate: true });
  };

  const { data, isLoading } = useQuery(
    [QUERY_KEY.getInstanceTypes, provider?.uuid],
    () => api.getInstanceTypes(provider?.uuid),
    {
      enabled: !!provider?.uuid,
      onSuccess: (data) => {
        // set default/first item as instance type after provider changes
        if (!getValues(INSTANCE_TYPE_FIELD) && provider?.code && data.length) {
          const defaultInstanceType =
            DEFAULT_INSTANCE_TYPES[provider.code] || data[0].instanceTypeCode;
          setValue(INSTANCE_TYPE_FIELD, defaultInstanceType, { shouldValidate: true });
        }
      }
    }
  );
  const instanceTypes = sortAndGroup(data, provider?.code);

  return (
    <Controller
      name={INSTANCE_TYPE_FIELD}
      control={control}
      rules={{
        required: t('universeForm.validation.required', {
          field: t('universeForm.instanceConfig.instanceType')
        }) as string
      }}
      render={({ field, fieldState }) => {
        const value =
          instanceTypes.find((i: InstanceTypeWithGroup) => i.instanceTypeCode === field.value) ??
          '';

        //empheral storage
        const isAWSEphemeralStorage =
          value && provider.code === CloudType.aws && isEphemeralAwsStorageInstance(value);
        const isGCPEphemeralStorage =
          value &&
          provider.code === CloudType.gcp &&
          deviceInfo?.storageType === StorageType.Scratch;

        return (
          <Box display="flex" width="100%">
            <YBLabel>{t('universeForm.instanceConfig.instanceType')}</YBLabel>
            <Box flex={1}>
              <YBAutoComplete
                loading={isLoading}
                value={(value as unknown) as Record<string, string>}
                options={(instanceTypes as unknown) as Record<string, string>[]}
                groupBy={(option: Record<string, string>) => option.groupName}
                getOptionLabel={getOptionLabel}
                renderOption={renderOption}
                onChange={handleChange}
                ybInputProps={{
                  error: !!fieldState.error,
                  helperText: fieldState.error?.message
                }}
              />

              {(isAWSEphemeralStorage || isGCPEphemeralStorage) && (
                <YBHelper variant={YBHelperVariants.warning}>
                  {t('universeForm.instanceConfig.ephemeralStorage')}
                </YBHelper>
              )}
            </Box>
          </Box>
        );
      }}
    />
  );
};
