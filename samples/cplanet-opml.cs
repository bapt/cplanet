<?xml version="1.0" ?>
<opml version="1.1">
<head>
	<title><?cs var:CPlanet:Name ?></title>
	<dateCreated>2009-05-15T12:19:14+02:00</dateCreated>
	<dateModified>2009-05-15T12:19:14+02:00</dateModified>
</head>
<body>
	<?cs each:feed = CPlanet.Feed ?>
	<outline text="<?cs var:feed.Name ?>" htmlUrl="<?cs var:feed.Home ?>" xmlUrl="<?cs var:feed.URL ?>" />
	<?cs /each ?>
</body>
</opml>
