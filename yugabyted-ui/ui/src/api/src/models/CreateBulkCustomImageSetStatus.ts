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
import type { CreateBulkCustomImageSetResponseInfo } from './CreateBulkCustomImageSetResponseInfo';
// eslint-disable-next-line no-duplicate-imports
import type { CustomImageSetSpec } from './CustomImageSetSpec';


/**
 * Custom Image bulk response Status
 * @export
 * @interface CreateBulkCustomImageSetStatus
 */
export interface CreateBulkCustomImageSetStatus  {
  /**
   * 
   * @type {CustomImageSetSpec}
   * @memberof CreateBulkCustomImageSetStatus
   */
  spec?: CustomImageSetSpec;
  /**
   * 
   * @type {CreateBulkCustomImageSetResponseInfo}
   * @memberof CreateBulkCustomImageSetStatus
   */
  info?: CreateBulkCustomImageSetResponseInfo;
}



