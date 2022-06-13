// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */


// eslint-disable-next-line no-duplicate-imports
import type { MigrationBackupInfo } from './MigrationBackupInfo';
// eslint-disable-next-line no-duplicate-imports
import type { MigrationBackupSpec } from './MigrationBackupSpec';


/**
 * Migration backup data
 * @export
 * @interface MigrationBackupData
 */
export interface MigrationBackupData  {
  /**
   * 
   * @type {MigrationBackupSpec}
   * @memberof MigrationBackupData
   */
  spec?: MigrationBackupSpec;
  /**
   * 
   * @type {MigrationBackupInfo}
   * @memberof MigrationBackupData
   */
  info?: MigrationBackupInfo;
}



