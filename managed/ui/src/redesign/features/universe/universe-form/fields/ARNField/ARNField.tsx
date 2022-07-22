import React, { ReactElement } from 'react';
import { Box } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useFormContext } from 'react-hook-form';
import { YBInputField, YBLabel } from '../../../../../components';
import { UniverseFormData } from '../../utils/dto';

interface ARNFieldProps {
  disabled?: boolean;
}

const ARN_FIELD_NAME = 'advancedConfig.awsArnString';

export const ARNField = ({ disabled }: ARNFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%">
      <YBLabel>{t('universeForm.advancedConfig.instanceProfileARN')}</YBLabel>
      <Box flex={1}>
        <YBInputField
          control={control}
          name={ARN_FIELD_NAME}
          fullWidth
          disabled={disabled}
          inputProps={{
            autoFocus: true,
            'data-testid': 'awsArnString'
          }}
        />
      </Box>
    </Box>
  );
};

//show only for aws provider
