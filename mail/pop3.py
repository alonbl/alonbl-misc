import configparser
import poplib

#creds.ini:
#[main]
#user = user
#password = password

config = configparser.ConfigParser()
config.read("creds.ini")

m = poplib.POP3_SSL("Outlook.office365.com", 995)
m.set_debuglevel(2)
m.user(config["main"]["user"])
m.pass_(config["main"]["password"])
