import React, { ReactElement } from 'react';
import { Box } from '@material-ui/core';
import { useFormContext } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { UniverseFormData } from '../../utils/dto';
import { YBLabel, YBToggleField } from '../../../../../components';

interface SystemDFieldProps {
  disabled: boolean;
}

const SYSTEMD_FIELD_NAME = 'advancedConfig.useSystemd';

export const SystemDField = ({ disabled }: SystemDFieldProps): ReactElement => {
  const { control } = useFormContext<UniverseFormData>();
  const { t } = useTranslation();

  return (
    <Box display="flex" width="100%">
      <YBLabel>{t('universeForm.advancedConfig.enableSystemD')}</YBLabel>
      <Box flex={1}>
        <YBToggleField
          name={SYSTEMD_FIELD_NAME}
          inputProps={{
            'data-testid': 'systemD'
          }}
          control={control}
          disabled={disabled}
        />
      </Box>
    </Box>
  );
};

//shown only after provider is selected
