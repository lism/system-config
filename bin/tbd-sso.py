#!/usr/bin/env python3
# -*- encoding: utf-8 -*-
import click
import subprocess

import os
# Generated by Selenium IDE
import traceback
import time
import re
from selenium.webdriver.common.keys import Keys
from selenium import webdriver
from selenium.webdriver.common.by import By
import selenium.webdriver.support.ui as ui
from selenium.webdriver.common.action_chains import ActionChains

chrome_options = webdriver.ChromeOptions()

data_dir = "--user-data-dir=/home/bhj/.config/google-chrome.tbd/"
chrome_options.add_argument(
    data_dir
)

# chrome_options.add_argument("--headless")

url = ("https://open.teambition.com/api/oauth/sso?redirect_uri=%s&app_id=%s&state=N/A") % (
  os.environ['TBD_REDIRECT_URL'],
  os.environ['TBD_APP_ID']
)

chrome_options.add_argument(
  url
)

driver = None

try:
  driver = webdriver.Chrome(options=chrome_options)
  actions = ActionChains(driver)
  wait = ui.WebDriverWait(driver, 10)  # timeout after 10 seconds

  driver.get(url)

  driver.set_window_size(550, 692)

  wait.until(
    lambda driver:
    re.match('.*/login|^' + os.environ['TBD_REDIRECT_URL'], driver.current_url)
  )

  if re.match("^" + os.environ['TBD_REDIRECT_URL'], driver.current_url):
    print (driver.current_url)
    exit(0)


  # 3 | click | css=.next-btn:nth-child(1) |
  driver.find_element(By.CSS_SELECTOR, ".next-btn:nth-child(1)").click()
  wait.until(
    lambda driver:
    re.match(".*/login/password", driver.current_url)
  )

  print("found login with password\n")
  driver.find_element(By.CSS_SELECTOR, ".account-input > input").click()
  driver.find_element(By.CSS_SELECTOR, ".account-input > input").send_keys(os.environ['TBD_USERNAME'])
  driver.find_element(By.CSS_SELECTOR, "input[placeholder='Password']").click()
  driver.find_element(By.CSS_SELECTOR, "input[placeholder='Password']").send_keys(os.environ['TBD_PASSWORD'])
  driver.find_element(By.CSS_SELECTOR, ".account-btn").click()
  wait.until(
    lambda driver:
    re.match("^" + os.environ['TBD_REDIRECT_URL'], driver.current_url)
  )

  print (driver.current_url)
finally:
  if driver:
    driver.quit()
