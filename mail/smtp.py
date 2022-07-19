import configparser
import smtplib

#creds.ini:
#[main]
#user = user
#password = password

config = configparser.ConfigParser()
config.read("creds.ini")

with smtplib.SMTP("smtp.office365.com", 587, local_hostname="xxx.exodigo.ai") as m:
    m.set_debuglevel(2)
    m.starttls()
    m.login(config["main"]["user"], config["main"]["password"])
