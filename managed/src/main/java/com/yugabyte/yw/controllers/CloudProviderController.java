// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CloudProviderFormData;
import com.yugabyte.yw.models.Customer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.yugabyte.yw.forms.OnPremFormData;
import com.yugabyte.yw.forms.NodeInstanceFormData;

import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Provider;

import com.fasterxml.jackson.databind.node.ObjectNode;

import com.google.inject.Inject;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

public class CloudProviderController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CloudProviderController.class);

  @Inject
  FormFactory formFactory;

  /**
   * GET endpoint for listing providers
   * @return JSON response with provider's
   */
  public Result list(UUID customerUUID) {
    List<Provider> providerList = Provider.getAll(customerUUID);
    return ok(Json.toJson(providerList));
  }

  /**
   * POST endpoint for creating new providers
   * @return JSON response of newly created provider
   */
  public Result create(UUID customerUUID) {
    Form<CloudProviderFormData> formData = formFactory.form(CloudProviderFormData.class).bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    // Since the Map<String, String> doesn't get parsed, so for now we would just
    // parse it from the requestBody
    JsonNode requestBody = request().body().asJson();
    Map<String, String> config = new HashMap<>();
    if (requestBody.has("config")) {
      config = Json.fromJson(requestBody.get("config"), Map.class);
    }
    try {
      Provider p = Provider.create(customerUUID, formData.get().code, formData.get().name, config);
      return ApiResponse.success(p);
    } catch (Exception e) {
      // TODO: Handle exception and print user friendly message
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  /**
   * POST endpoint for setting up an on-prem deployment
   * @param customerUuid the UUID of the customer performing the request
   * @return JSON response of newly created provider
   */
  public Result setupOnPrem(UUID customerUuid) {
    Form<OnPremFormData> formData = formFactory.form(OnPremFormData.class).bindFromRequest();
    ObjectNode responseJson = Json.newObject();

    if (formData.hasErrors()) {
      ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    UUID customerUUID = (UUID) ctx().args.get("customer_uuid");
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      ApiResponse.error(BAD_REQUEST, "Invalid Customer Context.");
    }

    // Get provider data or create a new provider.
    OnPremFormData.IdData identification = formData.get().identification;

    Provider p = null;
    if (identification.uuid == null) {
      if (identification.type == null || identification.name == null) {
        ApiResponse.error(BAD_REQUEST, "Field <identification> must have either " +
                "<uuid> or <name> and <type>");
      }

      p = Provider.get(customer.uuid, identification.name);
      if (p == null) {
        p = Provider.create(customer.uuid, identification.type, identification.name);
      }
    } else {
      p = Provider.find.byId(identification.uuid);
    }

    OnPremFormData.CloudData description = formData.get().description;
    LOG.info(Json.stringify(Json.toJson(formData.get())));
    if (description != null) {
      if (description.instanceTypes != null) {
        for (OnPremFormData.InstanceTypeData i : description.instanceTypes) {
          InstanceType it = InstanceType.get(p.code, i.code);
          if (it == null) {
            // TODO: instance type metadata?
            InstanceTypeDetails details = new InstanceTypeDetails();
            details.volumeDetailsList = i.volumeDetailsList;
            InstanceType.upsert(
                p.code,
                i.code,
                0, // numCores
                0.0, // memSizeGB
                0, // volumeCount
                0, // volumeSizeGB
                InstanceType.VolumeType.SSD,
                details
            );
          }
        }
      }
      if (description.regions != null) {
        for (OnPremFormData.RegionData r : description.regions) {
          Region region = Region.getByCode(r.code);
          if (region == null) {
            // TODO: region name vs code?
            region = Region.create(
                p,
                r.code, // code
                r.code, // name
                "" // ybImage
                );
          }
          if (r.zones != null) {
            for (OnPremFormData.ZoneData z : r.zones) {
              AvailabilityZone zone = AvailabilityZone.getByCode(z.code);
              if (zone == null) {
                // TODO: zone name vs node?
                zone = AvailabilityZone.create(
                    region,
                    z.code, // code
                    z.code, // name
                    "" // subnet
                );
              }
              if (z.nodes != null) {
                for (OnPremFormData.NodeData n : z.nodes) {
                  if (n.ip == null) {
                    responseJson.put("error", "Node required a valid IP field.");
                    return badRequest(responseJson);
                  }
                  // TODO: pass UUIDs once we revamp the Region and Zone paths above as well!
                  NodeInstance node = null;
                  if (node == null) {
                    InstanceType it = null;
                    if (n.instanceTypeCode != null) {
                      it = InstanceType.get(p.code, n.instanceTypeCode);
                    }
                    if (it == null) {
                      responseJson.put(
                          "error", "Invalid instance type code: " + n.instanceTypeCode);
                      return badRequest(responseJson);
                    }

                    NodeInstanceFormData details = new NodeInstanceFormData();
                    details.ip = n.ip;
                    details.sshPort = n.sshPort;
                    details.region = region.code;
                    details.zone = zone.code;
                    details.instanceType = it.getInstanceTypeCode();

                    // TODO: use this in the response anywhere?
                    node = NodeInstance.create(zone.uuid, details);
                  }
                }
              }
            }
          }
        }
      }
    }

    // TODO: placeholder
    OnPremFormData.AccessData access = formData.get().access;
    if (access != null) {
      responseJson.set("access", Json.toJson(access));
    }

    return ApiResponse.success(responseJson);
  }
}
