// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.BACKUP_LOCATION_FIELDNAME;
import static play.mvc.Http.Status.CONFLICT;

import com.amazonaws.services.s3.AmazonS3;
import com.amazonaws.services.s3.model.AmazonS3Exception;
import com.azure.storage.blob.BlobContainerClient;
import com.azure.storage.blob.models.BlobStorageException;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.cloud.storage.Storage;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.configs.CloudClientsFactory;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigType;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.configs.data.CustomerConfigPasswordPolicyData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageAzureData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageGCSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageS3Data;
import com.yugabyte.yw.models.configs.validators.ConfigDataValidator;
import com.yugabyte.yw.models.configs.validators.CustomerConfigPasswordPolicyValidator;
import com.yugabyte.yw.models.configs.validators.CustomerConfigStorageAzureValidator;
import com.yugabyte.yw.models.configs.validators.CustomerConfigStorageGCSValidator;
import com.yugabyte.yw.models.configs.validators.CustomerConfigStorageNFSValidator;
import com.yugabyte.yw.models.configs.validators.CustomerConfigStorageS3Validator;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.inject.Inject;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.validator.routines.DomainValidator;

@Singleton
public class CustomerConfigValidator {

  private static final String[] TLD_OVERRIDE = {"local"};

  private final BeanValidator beanValidator;

  private final CloudClientsFactory factory;

  static {
    DomainValidator.updateTLDOverride(DomainValidator.ArrayType.LOCAL_PLUS, TLD_OVERRIDE);
  }

  private final Map<Class<? extends CustomerConfigData>, ConfigDataValidator> validators =
      new HashMap<>();

  @Inject
  public CustomerConfigValidator(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
    this.factory = createCloudFactory();

    validators.put(
        CustomerConfigStorageGCSData.class,
        new CustomerConfigStorageGCSValidator(beanValidator, factory));
    validators.put(
        CustomerConfigStorageS3Data.class,
        new CustomerConfigStorageS3Validator(beanValidator, factory));
    validators.put(
        CustomerConfigStorageNFSData.class, new CustomerConfigStorageNFSValidator(beanValidator));
    validators.put(
        CustomerConfigStorageAzureData.class,
        new CustomerConfigStorageAzureValidator(beanValidator, factory));
    validators.put(
        CustomerConfigStorageGCSData.class,
        new CustomerConfigStorageGCSValidator(beanValidator, factory));
    validators.put(
        CustomerConfigPasswordPolicyData.class,
        new CustomerConfigPasswordPolicyValidator(beanValidator));
  }

  /**
   * Validates data which is contained in formData. During the procedure it calls all the registered
   * validators. Errors are collected and returned back as a result. Empty result object means no
   * errors.
   *
   * <p>Currently are checked: - NFS - NFS Storage Path (against regexp NFS_PATH_REGEXP); - S3/AWS -
   * S3 Bucket, S3 Bucket Host Base (both as URLs); - GCS - GCS Bucket (as URL); - AZURE - Container
   * URL (as URL).
   *
   * <p>The URLs validation allows empty scheme. In such case the check is made with DEFAULT_SCHEME
   * added before the URL.
   *
   * @param customerConfig
   */
  public void validateConfig(CustomerConfig customerConfig) {
    beanValidator.validate(customerConfig);

    String configName = customerConfig.getConfigName();
    CustomerConfig existentConfig = CustomerConfig.get(customerConfig.customerUUID, configName);
    if (existentConfig != null) {
      if (!existentConfig.getConfigUUID().equals(customerConfig.getConfigUUID())) {
        beanValidator
            .error()
            .code(CONFLICT)
            .forField("configName", String.format("Configuration %s already exists", configName))
            .throwError();
      }

      JsonNode newBackupLocation = customerConfig.getData().get(BACKUP_LOCATION_FIELDNAME);
      JsonNode oldBackupLocation = existentConfig.getData().get(BACKUP_LOCATION_FIELDNAME);
      if (newBackupLocation != null
          && oldBackupLocation != null
          && !StringUtils.equals(newBackupLocation.textValue(), oldBackupLocation.textValue())) {
        String errorMsg = "Field is read-only.";
        throwBeanValidatorError(BACKUP_LOCATION_FIELDNAME, errorMsg);
      }
    }

    CustomerConfigData data = customerConfig.getDataObject();
    beanValidator.validate(data, "data");
    ConfigDataValidator validator = validators.get(data.getClass());
    if (validator != null) {
      validator.validate(data);
    }
  }

  public void validateConfigRemoval(CustomerConfig customerConfig) {
    if (customerConfig.getType() == ConfigType.STORAGE) {
      List<Backup> backupList = Backup.getInProgressAndCompleted(customerConfig.getCustomerUUID());
      backupList =
          backupList
              .stream()
              .filter(
                  b -> b.getBackupInfo().storageConfigUUID.equals(customerConfig.getConfigUUID()))
              .collect(Collectors.toList());
      if (!backupList.isEmpty()) {
        beanValidator
            .error()
            .global(
                String.format(
                    "Configuration %s is used in backup and can't be deleted",
                    customerConfig.getConfigName()))
            .throwError();
      }
      List<Schedule> scheduleList =
          Schedule.getActiveBackupSchedules(customerConfig.getCustomerUUID());
      // This should be safe to do since storageConfigUUID is a required constraint.
      scheduleList =
          scheduleList
              .stream()
              .filter(
                  s ->
                      s.getTaskParams()
                          .path("storageConfigUUID")
                          .asText()
                          .equals(customerConfig.getConfigUUID().toString()))
              .collect(Collectors.toList());
      if (!scheduleList.isEmpty()) {
        beanValidator
            .error()
            .global(
                String.format(
                    "Configuration %s is used in scheduled backup and can't be deleted",
                    customerConfig.getConfigName()))
            .throwError();
      }
    }
  }

  private class CloudClientsFactoryImpl implements CloudClientsFactory {
    @Override
    public Storage createGcpStorage(CustomerConfigStorageGCSData configData)
        throws IOException, UnsupportedEncodingException {
      return GCPUtil.getStorageService(configData);
    }

    @Override
    public BlobContainerClient createBlobContainerClient(
        String azUrl, String azSasToken, String container) throws BlobStorageException {
      return AZUtil.createBlobContainerClient(azUrl, azSasToken, container);
    }

    @Override
    public AmazonS3 createS3Client(CustomerConfigStorageS3Data configData)
        throws AmazonS3Exception {
      return AWSUtil.createS3Client(configData);
    }
  }

  protected CloudClientsFactory createCloudFactory() {
    return new CloudClientsFactoryImpl();
  }

  private void throwBeanValidatorError(String fieldName, String exceptionMsg) {
    beanValidator
        .error()
        .forField(ConfigDataValidator.fieldFullName(fieldName), exceptionMsg)
        .throwError();
  }
}
