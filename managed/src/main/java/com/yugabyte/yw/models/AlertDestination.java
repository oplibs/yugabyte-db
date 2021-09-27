// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonProperty;
import io.ebean.ExpressionList;
import io.ebean.Finder;
import io.ebean.Model;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.persistence.Column;
import javax.persistence.Entity;
import javax.persistence.FetchType;
import javax.persistence.Id;
import javax.persistence.JoinColumn;
import javax.persistence.JoinTable;
import javax.persistence.ManyToMany;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;
import lombok.AccessLevel;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.Getter;
import lombok.NonNull;
import lombok.Setter;
import lombok.ToString;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@EqualsAndHashCode(callSuper = false, doNotUseGetters = true)
@Entity
public class AlertDestination extends Model {

  @Id
  @Column(nullable = false, unique = true)
  private UUID uuid;

  @NotNull
  @Column(nullable = false)
  private UUID customerUUID;

  @NotNull
  @Size(min = 1, max = 63)
  @Column(columnDefinition = "Text", nullable = false)
  private String name;

  @ToString.Exclude
  @NotNull
  @Size(min = 1)
  @Getter(AccessLevel.NONE)
  @Setter(AccessLevel.NONE)
  @ManyToMany(fetch = FetchType.LAZY)
  @JoinTable(
      name = "alert_destination_group",
      joinColumns = {
        @JoinColumn(
            name = "destination_uuid",
            referencedColumnName = "uuid",
            nullable = false,
            updatable = false)
      },
      inverseJoinColumns = {
        @JoinColumn(
            name = "channel_uuid",
            referencedColumnName = "uuid",
            nullable = false,
            updatable = false)
      })
  private Set<AlertChannel> channels;

  @NotNull
  @Column(nullable = false)
  private boolean defaultDestination = false;

  private static final Finder<UUID, AlertDestination> find =
      new Finder<UUID, AlertDestination>(AlertDestination.class) {};

  @JsonProperty
  public List<UUID> getChannels() {
    return channels.stream().map(AlertChannel::getUuid).collect(Collectors.toList());
  }

  @JsonIgnore
  public List<AlertChannel> getChannelsList() {
    return new ArrayList<>(channels);
  }

  public AlertDestination setChannelsList(@NonNull List<AlertChannel> channels) {
    this.channels = new HashSet<>(channels);
    return this;
  }

  public AlertDestination generateUUID() {
    this.uuid = UUID.randomUUID();
    return this;
  }

  public static AlertDestination get(UUID customerUUID, UUID destinationUUID) {
    return createQuery().idEq(destinationUUID).eq("customerUUID", customerUUID).findOne();
  }

  public static ExpressionList<AlertDestination> createQuery() {
    return find.query().where();
  }
}
