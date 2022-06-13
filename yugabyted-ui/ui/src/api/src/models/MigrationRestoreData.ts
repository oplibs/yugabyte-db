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
import type { MigrationRestoreInfo } from './MigrationRestoreInfo';
// eslint-disable-next-line no-duplicate-imports
import type { MigrationRestoreSpec } from './MigrationRestoreSpec';


/**
 * Restore Backup data
 * @export
 * @interface MigrationRestoreData
 */
export interface MigrationRestoreData  {
  /**
   * 
   * @type {MigrationRestoreSpec}
   * @memberof MigrationRestoreData
   */
  spec?: MigrationRestoreSpec;
  /**
   * 
   * @type {MigrationRestoreInfo}
   * @memberof MigrationRestoreData
   */
  info?: MigrationRestoreInfo;
}



