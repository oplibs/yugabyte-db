name := """yugaware"""
import com.typesafe.sbt.packager.MappingsHelper._

version := "1.0-SNAPSHOT"

lazy val root = (project in file("."))
  .enablePlugins(PlayJava, PlayEbean, SbtWeb, JavaAppPackaging, DockerPlugin)
  .disablePlugins(PlayLayoutPlugin)


scalaVersion := "2.11.7"

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs,
  filters,
  "org.mockito" % "mockito-core" % "1.10.19",
  "org.mindrot" % "jbcrypt" % "0.3m",
  "mysql" % "mysql-connector-java" % "5.1.27",
  "org.postgresql" % "postgresql" % "9.2-1003-jdbc4",
  "org.apache.httpcomponents" % "httpcore" % "4.4.5",
  "org.apache.httpcomponents" % "httpclient" % "4.5.2",
  "org.flywaydb" %% "flyway-play" % "3.0.1"
)
resolvers += "Yugabyte S3 Snapshots" at "s3://no-such-url/"
// resolvers += Resolver.mavenLocal
libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT"
libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT" % "compile,test" classifier "tests"
publishTo := Some("yugabyteS3" at "s3://no-such-url/")

javaOptions in Test += "-Dconfig.file=src/main/resources/application.test.conf"

// Skip packaging javadoc for now
mappings in (Compile, packageDoc) := Seq()

topLevelDirectory := None

dockerExposedPorts := Seq(9000)
dockerRepository := Some("registry.replicated.com/yugaware_apple")
