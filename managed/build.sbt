name := """yugaware"""

version := "1.0-SNAPSHOT"

lazy val root = (project in file(".")).enablePlugins(PlayJava, PlayEbean, SbtWeb)

scalaVersion := "2.11.7"

libraryDependencies ++= Seq(
  javaJdbc,
  cache,
  javaWs,
  "org.mockito" % "mockito-core" % "1.10.19",
  "org.mindrot" % "jbcrypt" % "0.3m",
  "mysql" % "mysql-connector-java" % "5.1.27",
  "org.postgresql" % "postgresql" % "9.2-1003-jdbc4",

  // WebJars (i.e. client-side) dependencies
  "org.webjars" %  "jquery" % "2.1.1",
  "org.webjars" %  "bootstrap" % "3.3.1",
  "org.webjars" %  "font-awesome" % "4.6.3",
  "org.webjars" %  "metisMenu" % "1.1.2",
  "org.webjars" %  "leaflet" % "0.7.7",
  "org.webjars" %  "mousetrap" % "1.5.3-1",
  "org.webjars.bower" % "react" % "15.2.1",
  "org.webjars" % "select2" % "4.0.2"
)

resolvers += "Yugabyte S3 Snapshots" at "s3://no-such-url/"
// resolvers += Resolver.mavenLocal
libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT"
libraryDependencies += "org.yb" % "yb-client" % "0.8.0-SNAPSHOT" % "compile,test" classifier "tests"
publishTo := Some("yugabyteS3" at "s3://no-such-url/")

topLevelDirectory := None
